//  Copyright 2019 U.C. Berkeley RISE Lab
//
//  Licensed under the Apache License, Version 2.0 (the "License");
//  you may not use this file except in compliance with the License.
//  You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
//  Unless required by applicable law or agreed to in writing, software
//  distributed under the License is distributed on an "AS IS" BASIS,
//  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//  See the License for the specific language governing permissions and
//  limitations under the License.

#include <iostream>
#include "monitor/monitoring_handlers.hpp"
#include "monitor/monitoring_utils.hpp"
#include "monitor/policies.hpp"
#include "yaml-cpp/yaml.h"

unsigned kMemoryThreadCount;
unsigned kEbsThreadCount;

unsigned kMemoryNodeCapacity;
unsigned kEbsNodeCapacity;

unsigned kDefaultGlobalMemoryReplication;
unsigned kDefaultGlobalEbsReplication;
unsigned kDefaultLocalReplication;
unsigned kMinimumReplicaNumber;

bool kEnableElasticity;
bool kEnableTiering;
bool kEnableSelectiveRep;

// read-only per-tier metadata
hmap<Tier, TierMetadata, TierEnumHash> kTierMetadata;

ZmqUtil zmq_util;
ZmqUtilInterface *kZmqUtil = &zmq_util;

HashRingUtil hash_ring_util;
HashRingUtilInterface *kHashRingUtil = &hash_ring_util;

int main(int argc, char *argv[]) {
    auto log = spdlog::basic_logger_mt("monitoring_log", "log_monitoring.txt", true);
    log->flush_on(spdlog::level::info);

    if (argc != 1) {
        std::cerr << "Usage: " << argv[0] << std::endl;
        return 1;
    }

    // read the YAML conf
    YAML::Node conf = YAML::LoadFile("conf/anna-config.yml");
    YAML::Node monitoring = conf["monitoring"];
    Address ip = monitoring["ip"].as<Address>();
    Address management_ip = monitoring["mgmt_ip"].as<Address>();

    YAML::Node policy = conf["policy"];
    kEnableElasticity = policy["elasticity"].as<bool>();
    kEnableSelectiveRep = policy["selective-rep"].as<bool>();
    kEnableTiering = policy["tiering"].as<bool>();

    log->info("Elasticity policy enabled: {}", kEnableElasticity);
    log->info("Tiering policy enabled: {}", kEnableTiering);
    log->info("Selective replication policy enabled: {}", kEnableSelectiveRep);

    YAML::Node threads = conf["threads"];
    kMemoryThreadCount = threads["memory"].as<unsigned>();
    kEbsThreadCount = threads["ebs"].as<unsigned>();

    YAML::Node capacities = conf["capacities"];
    kMemoryNodeCapacity = capacities["memory-cap"].as<unsigned>() * 1000000;
    kEbsNodeCapacity = capacities["ebs-cap"].as<unsigned>() * 1000000;

    YAML::Node replication = conf["replication"];
    kDefaultGlobalMemoryReplication = replication["memory"].as<unsigned>();
    kDefaultGlobalEbsReplication = replication["ebs"].as<unsigned>();
    kDefaultLocalReplication = replication["local"].as<unsigned>();
    kMinimumReplicaNumber = replication["minimum"].as<unsigned>();

    kTierMetadata[Tier::MEMORY] =
            TierMetadata(Tier::MEMORY, kMemoryThreadCount,
                         kDefaultGlobalMemoryReplication, kMemoryNodeCapacity);
    kTierMetadata[Tier::DISK] =
            TierMetadata(Tier::DISK, kEbsThreadCount, kDefaultGlobalEbsReplication,
                         kEbsNodeCapacity);

    GlobalRingMap global_hash_rings;
    LocalRingMap local_hash_rings;

    // form local hash rings
    for (const auto &pair : kTierMetadata) {
        TierMetadata tier = pair.second;
        for (unsigned tid = 0; tid < tier.thread_number_; tid++) {
            local_hash_rings[tier.id_].insert(ip, ip, 0, tid);
        }
    }

    // keep track of the keys' replication info
    map<Key, KeyReplication> key_replication_map;

    // the set of changes made on this thread since the last round of gossip
    set<Key> local_change_set;

    unsigned memory_node_count;
    unsigned ebs_node_count;

    // The order: Key, Address from where the report is coming,
    // Address from where the key was accessed
    map<Key, map<Address, map<Address, unsigned>>> key_get_access_frequency;
    map<Key, map<Address, map<Address, unsigned>>> key_put_access_frequency;

    map<Key, map<Address, map<Address, unsigned>>> key_prev_get_access;
    map<Key, map<Address, map<Address, unsigned>>> key_prev_put_access;

    map<Key, unsigned> key_access_summary;

    map<Key, unsigned> key_size;

    StorageStats memory_storage;

    StorageStats ebs_storage;

    OccupancyStats memory_occupancy;

    OccupancyStats ebs_occupancy;

    AccessStats memory_accesses;

    AccessStats ebs_accesses;

    SummaryStats ss;

    map<string, double> user_latency;

    map<string, double> user_throughput;

    map<Key, std::pair<double, unsigned>> latency_miss_ratio_map;

    // delay matrix between the key-value servers
    map<Key, map<Key, double>> delay_matrix;

    map<Key, set<std::string>> slaves_of_master;
    map<Key, map<Address, double>> writer_rates;
    map<Key, map<Address, double>> reader_rates;

    map<std::string, NodeAsPlacementDest> host_statuses;

    vector<Address> routing_ips;

    MonitoringThread mt = MonitoringThread(ip);

    zmq::context_t context(1);
    SocketCache pushers(&context, ZMQ_PUSH);

    // responsible for listening to the response of the replication factor change
    // request
    zmq::socket_t response_puller(context, ZMQ_PULL);
    int timeout = 10000;

    response_puller.setsockopt(ZMQ_RCVTIMEO, &timeout, sizeof(timeout));
    response_puller.bind(mt.response_bind_address());

    // keep track of departing node status
    map<Address, unsigned> departing_node_map;

    // responsible for both node join and departure
    zmq::socket_t notify_puller(context, ZMQ_PULL);
    notify_puller.bind(mt.notify_bind_address());

    // responsible for receiving depart done notice
    zmq::socket_t depart_done_puller(context, ZMQ_PULL);
    depart_done_puller.bind(mt.depart_done_bind_address());

    // responsible for receiving feedback from users
    zmq::socket_t feedback_puller(context, ZMQ_PULL);
    feedback_puller.bind(mt.feedback_report_bind_address());

    zmq::socket_t delay_report_puller(context, ZMQ_PULL);
    delay_report_puller.bind(mt.delay_report_bind_address());

    zmq::socket_t key_request_puller(context, ZMQ_PULL);
    key_request_puller.bind(mt.key_request_bind_address());

    vector<zmq::pollitem_t> pollitems = {
            {static_cast<void *>(notify_puller),       0, ZMQ_POLLIN, 0},
            {static_cast<void *>(depart_done_puller),  0, ZMQ_POLLIN, 0},
            {static_cast<void *>(feedback_puller),     0, ZMQ_POLLIN, 0},
            {static_cast<void *>(delay_report_puller), 0, ZMQ_POLLIN, 0},
            {static_cast<void *>(key_request_puller),  0, ZMQ_POLLIN, 0}};

    auto report_start = std::chrono::system_clock::now();
    auto report_end = std::chrono::system_clock::now();
    auto update_start = std::chrono::system_clock::now();
    auto update_end = std::chrono::system_clock::now();

    auto grace_start = std::chrono::system_clock::now();

    unsigned new_memory_count = 0;
    unsigned new_ebs_count = 0;
    bool removing_memory_node = false;
    bool removing_ebs_node = false;

    unsigned server_monitoring_epoch = 0;

    unsigned rid = 0;

    unsigned update_iter = 0;

    while (true) {
        kZmqUtil->poll(0, &pollitems);

        if (pollitems[0].revents & ZMQ_POLLIN) {
            string serialized = kZmqUtil->recv_string(&notify_puller);
            membership_handler(log, serialized, global_hash_rings, new_memory_count,
                               new_ebs_count, grace_start, routing_ips,
                               memory_storage, ebs_storage, memory_occupancy,
                               ebs_occupancy, key_get_access_frequency, key_put_access_frequency);
        }

        if (pollitems[1].revents & ZMQ_POLLIN) {
            string serialized = kZmqUtil->recv_string(&depart_done_puller);
            depart_done_handler(log, serialized, departing_node_map, management_ip,
                                removing_memory_node, removing_ebs_node, pushers,
                                grace_start);
        }

        if (pollitems[2].revents & ZMQ_POLLIN) {
            string serialized = kZmqUtil->recv_string(&feedback_puller);
            feedback_handler(serialized, user_latency, user_throughput,
                             latency_miss_ratio_map);
        }

        if (pollitems[3].revents & ZMQ_POLLIN) {
            string serialized = kZmqUtil->recv_string(&delay_report_puller);
            DelayReport delay_report;
            delay_report.ParseFromString(serialized);
            //log->info("DEBUG: Delay report received {} <-> {} : {}", delay_report.reporter_server_address(),
            //          delay_report.measured_server_address(), delay_report.latency());

            // FIXME: How to use the latency values?
            if(delay_report.reporter_server_address() == delay_report.measured_server_address()){
                delay_matrix[delay_report.reporter_server_address()][delay_report.measured_server_address()] = 0;
                delay_matrix[delay_report.measured_server_address()][delay_report.reporter_server_address()] = 0;
            }
            else {
                delay_matrix[delay_report.reporter_server_address()][delay_report.measured_server_address()] =
                        (delay_matrix[delay_report.reporter_server_address()][delay_report.measured_server_address()] +
                         delay_report.latency()) / 2;
                delay_matrix[delay_report.measured_server_address()][delay_report.reporter_server_address()] =
                        (delay_matrix[delay_report.measured_server_address()][delay_report.reporter_server_address()] +
                         delay_report.latency()) / 2;
            }
            /*
            log->info("DEBUG: DELAY MATRIX [{}][{}] = {}", delay_report.reporter_server_address(),
                      delay_report.measured_server_address(),
                      delay_matrix[delay_report.reporter_server_address()][delay_report.measured_server_address()]);
            */
        }

        if (pollitems[4].revents & ZMQ_POLLIN) {
            string serialized = kZmqUtil->recv_string(&key_request_puller);
            std::cout << "MONITORING: KeyRequest received.\n";
            flooding_placement_handler(log, serialized, global_hash_rings, pushers, key_replication_map,
                                       local_change_set, delay_matrix, host_statuses, slaves_of_master, writer_rates,
                                       reader_rates, response_puller);
            std::cout << "MONITORING: KeyRequest handled :)\n";
        }


        report_end = std::chrono::system_clock::now();

        if (std::chrono::duration_cast<std::chrono::seconds>(report_end - report_start).count() >=
            kMonitoringThreshold) {

            /////////////////////////////////////////////////////////////////////////
            log->info("\n\n\n\n\n\nDEBUG: -----------------------------------------------------------------");
            log->info("DEBUG: Connected KVS servers to the cluster (round {}):", server_monitoring_epoch);
            for (const ServerThread &st : global_hash_rings[Tier::MEMORY].get_unique_servers()) {
                log->info("DEBUG:\t - {}", st.id());
            }
            log->info("DEBUG: *****************************************************************");
            log->info("DEBUG: Content of the key_replication_map");
            for (const auto &key_rep : key_replication_map) {
                if (key_rep.first.find("METADATA") == std::string::npos) {
                    log->info("DEBUG:\t - Key: '{}'\t Master address: '{}'\tSlave addresses:", key_rep.first,
                              key_rep.second.master_address_);
                    for (const auto &slave : key_rep.second.slave_addresses_) {
                        log->info("DEBUG:\t\t\t\t\t\t *  {}", slave);
                    }
                }
            }
            log->info("DEBUG: *****************************************************************");
            log->info("DEBUG: Current key mapping:");
            for (const auto &host_mapping : host_statuses) {
                log->info("DEBUG:Host {}:", host_mapping.second.st.public_ip());
                log->info("DEBUG:\tIP: {}", host_mapping.second.st.public_ip());
                log->info("DEBUG:\tFree capacity: {}", host_mapping.second.capacity - host_mapping.second.load);
                log->info("DEBUG:\tStored replicas:");
                for (const auto &replica :host_mapping.second.replicas) {
                    log->info("DEBUG:\t\t - {}", replica);

                }
            }
            log->info("DEBUG: *****************************************************************");
            log->info("DEBUG: Writing rates:");
            for (const auto &write_rate : writer_rates) {
                log->info("DEBUG:\t Key {} is written by:", write_rate.first);
                for (const auto &host_write : write_rate.second) {
                    log->info("DEBUG:\t\t - Host {} with rate {}", host_write.first, host_write.second);
                }
            }
            log->info("DEBUG: Reading rates:");
            for (const auto &read_rate : reader_rates) {
                log->info("DEBUG:\t Key {} is read by:", read_rate.first);
                for (const auto &host_read : read_rate.second) {
                    log->info("DEBUG:\t\t - Host {} with rate {}", host_read.first, host_read.second);
                }
            }
            log->info("DEBUG: *****************************************************************");
            log->info("DEBUG: Delay matrix:");
            for (const auto map_of_map : delay_matrix) {
                for (const auto delay_row : map_of_map.second) {
                    log->info("DEBUG:\t [{}][{}] = {}", map_of_map.first, delay_row.first,
                              delay_matrix[map_of_map.first][delay_row.first]);
                }
            }
            log->info("DEBUG: -----------------------------------------------------------------");
            /////////////////////////////////////////////////////////////////////////

            server_monitoring_epoch += 1;

            memory_node_count =
                    global_hash_rings[Tier::MEMORY].size() / kVirtualThreadNum;
            ebs_node_count = global_hash_rings[Tier::DISK].size() / kVirtualThreadNum;

            key_get_access_frequency.clear();
            key_put_access_frequency.clear();
            key_access_summary.clear();

            memory_storage.clear();
            ebs_storage.clear();

            memory_occupancy.clear();
            ebs_occupancy.clear();

            ss.clear();

            user_latency.clear();
            user_throughput.clear();
            latency_miss_ratio_map.clear();

            collect_internal_stats(
                    global_hash_rings, local_hash_rings, pushers, mt, response_puller,
                    log, rid, key_get_access_frequency, key_put_access_frequency, key_size, memory_storage, ebs_storage,
                    memory_occupancy, ebs_occupancy, memory_accesses, ebs_accesses, key_prev_get_access,
                    key_prev_put_access);

            compute_summary_stats(key_get_access_frequency, key_put_access_frequency, memory_storage, ebs_storage,
                                  memory_occupancy, ebs_occupancy, memory_accesses,
                                  ebs_accesses, key_access_summary, ss, log,
                                  server_monitoring_epoch);

            collect_external_stats(user_latency, user_throughput, ss, log);

            // initialize replication factor for new keys
            for (const auto &key_access_pair : key_access_summary) {
                Key key = key_access_pair.first;
                if (!is_metadata(key) &&
                    key_replication_map.find(key) == key_replication_map.end()) {
                    init_replication(key_replication_map, key);
                }
            }

            storage_policy(log, global_hash_rings, grace_start, ss, memory_node_count,
                           ebs_node_count, new_memory_count, new_ebs_count,
                           removing_ebs_node, management_ip, mt, departing_node_map,
                           pushers);

            movement_policy(log, global_hash_rings, local_hash_rings, grace_start, ss,
                            memory_node_count, ebs_node_count, new_memory_count,
                            new_ebs_count, management_ip, key_replication_map,
                            key_access_summary, key_size, mt, pushers,
                            response_puller, routing_ips, rid);

            slo_policy(log, global_hash_rings, local_hash_rings, grace_start, ss,
                       memory_node_count, new_memory_count, removing_memory_node,
                       management_ip, key_replication_map, key_access_summary, mt,
                       departing_node_map, pushers, response_puller, routing_ips, rid,
                       latency_miss_ratio_map);

            report_start = std::chrono::system_clock::now();

            // MIGRATION MODULE  ///////////////////////////////////////////////////////////////////////////////////////////////////
            log->info("DEBUG: MIGRATION MODULE");
            set<Address> keys_for_replacement;
            keys_for_replacement.clear();
            double change = 0.2;

            // checking writing rates
            for (const auto &key_access_pair : key_put_access_frequency) {
                Key key = key_access_pair.first;
                map<Key, map<Address, double>> tmp_writer_rates;
                tmp_writer_rates.clear();
                if (key.find("METADATA") == std::string::npos) {
                    for (const auto &per_machine_pair : key_access_pair.second) {
                        for (const auto &per_requester_pair : per_machine_pair.second) {
                            tmp_writer_rates[key][per_requester_pair.first] += per_requester_pair.second;
                        }
                    }
                }

                for (const auto it : tmp_writer_rates[key]) {
                    if (writer_rates.find(key) == writer_rates.end()) {
                        writer_rates[key][it.first] = it.second;
                        keys_for_replacement.insert(key);
                    } else if ((writer_rates[key][it.first] * (1.0 + change) < it.second ||
                                writer_rates[key][it.first] * (1.0 - change) > it.second)) {
                        log->info("DEBUG: \tKey {}'s access pattern has changed mor than 20% in the last iteration.",
                                  key);
                        log->info("DEBUG: \tFrom host {}, old writing: {} and new writing: {}", it.first,
                                  writer_rates[key][it.first], it.second);
                        writer_rates[key][it.first] = it.second;
                        if (it.second != 0) {
                            keys_for_replacement.insert(key);
                        }
                    }
                }
            }

            // checking reading rates
            for (const auto &key_access_pair : key_get_access_frequency) {
                Key key = key_access_pair.first;
                map<Key, map<Address, double>> tmp_reader_rates;
                if (key.find("METADATA") == std::string::npos) {
                    //log->info("DEBUG: \t- key: {}", key);
                    for (const auto &per_machine_pair : key_access_pair.second) {
                        //log->info("DEBUG: \t\t- report from {}", per_machine_pair.first);
                        for (const auto &per_requester_pair : per_machine_pair.second) {
                            //log->info("DEBUG: \t\t\t- get {} times from {}", per_requester_pair.second,
                            //          per_requester_pair.first);

                            tmp_reader_rates[key][per_requester_pair.first] += per_requester_pair.second;
                        }
                    }
                }

                for (const auto it : tmp_reader_rates[key]) {
                    //log->info("DEBUG: Checking {}, from host {}, old reading: {} and new reading: {}", key,
                    //          it.first,
                    //          reader_rates[key][it.first], it.second);
                    if (reader_rates.find(key) == reader_rates.end()) {
                        //log->info("DEBUG: Key {} is not among the key_rates yet", key);
                        reader_rates[key][it.first] = it.second;
                        keys_for_replacement.insert(key);
                    } else if ((reader_rates[key][it.first] * (1.0 + change) < it.second ||
                                reader_rates[key][it.first] * (1.0 - change) > it.second)) {
                        log->info("DEBUG: Key {}'s access pattern has changed mor than 20% in the last iteration.",
                                  key);
                        log->info("DEBUG: From host {}, old reading: {} and new reading: {}", it.first,
                                  reader_rates[key][it.first], it.second);
                        reader_rates[key][it.first] = it.second;
                        if (it.second != 0) {
                            keys_for_replacement.insert(key);
                        }
                    }
                }

            }

            // Initiating replacement of keys
            KeyRequest replacement_request;
            replacement_request.set_type(RequestType::PUT);
            replacement_request.set_response_address(mt.response_connect_address());
            // FIXME:
            replacement_request.set_request_id("todo");
            for (const auto key: keys_for_replacement) {
                log->info("DEBUG: \tINTIATE KEY '{}' REPLACEMENT", key);

                KeyTuple *tp = replacement_request.add_tuples();
                tp->set_key(key);
                tp->set_lattice_type(LatticeType::LWW);

                //TODO: Sending the GET request
                KeyRequest request;
                request.set_request_id("TODO");
                request.set_response_address(mt.response_connect_address());
                KeyTuple *get_tp = request.add_tuples();
                get_tp->set_key(key);
                request.set_type(RequestType::GET);

                string serialized_req;
                request.SerializeToString(&serialized_req);
                // Find destination KVS
                Address target_server;
                for (const ServerThread &st : global_hash_rings[Tier::MEMORY].get_unique_servers()) {
                    if (st.public_ip() == key_replication_map[key].master_address_) {
                        target_server = st.key_request_connect_address();
                    }
                }
                kZmqUtil->send_string(serialized_req, &pushers[target_server]);
                log->info("DEBUG: \t\tSending GET request about key '{}' to {}", key, target_server);

                // TODO: Saving the returned payload
                bool succeed;
                vector<KeyResponse> responses;
                set<string> req_ids{request.request_id()};
                log->info("DEBUG: \t\tWaiting on address {} for receive the answer with id '{}' ...",
                          mt.response_bind_address(), request.request_id());
                log->info("Let's try receiving");
                //receive(zmq::socket_t & recv_socket, set<string> & request_ids, vector<RES> & responses)
                //succeed = receive<KeyResponse>(pushers[mt.response_bind_address()], req_ids, responses);
                /////////////////////////////////////////////////////////////////////////////////////////////////////////
                // FIXME: Change to call receive method
                zmq::message_t message;

                // We allow as many timeouts as there are requests that we made. We may want
                // to make this configurable at some point.
                unsigned timeout_limit = req_ids.size();

                while (true) {
                    KeyResponse response;

                    log->info("If message received");
                    if (response_puller.recv(&message)) {
                        log->info("Read the message");
                        string serialized_resp = kZmqUtil->message_to_string(message);
                        response.ParseFromString(serialized_resp);

                        string resp_id = response.response_id();
                        log->info("Save response ID: {}", resp_id);

                        if (req_ids.find(resp_id) != req_ids.end()) {
                            log->info("If the ID is one of that we're looking for");
                            req_ids.erase(resp_id);
                            log->info("save message");
                            responses.push_back(response);
                        }

                        if (req_ids.size() == 0) {
                            succeed = true;
                            break;
                        }
                    } else {
                        // We assume that the request timed out here, so errno should always equal
                        // EAGAIN. If that is not the case, log or print the errno here for
                        // debugging.
                        timeout_limit--;

                        log->info("Message didn't receive");
                        if (timeout_limit == 0) {
                            log->info("Timeout");
                            responses.clear();
                            succeed = false;
                            break;
                        }
                    }
                }
                ///////////////////////////////////////////////////////////////////////////////////////////////////////////

                log->info("DEBUG: TEST");

                if (succeed) {

                    tp->set_payload(responses[0].tuples()[0].payload());

                    log->info("DEBUG: \t\tGot it :)");
                    string serialized_replace_req;
                    replacement_request.SerializeToString(&serialized_replace_req);

                    log->info("DEBUG: \t\tCalling Replacement handler");
                    flooding_placement_handler(log, serialized_replace_req, global_hash_rings, pushers,
                                               key_replication_map,
                                               local_change_set, delay_matrix, host_statuses, slaves_of_master,
                                               writer_rates,
                                               reader_rates, response_puller, true);
                }

            }
            keys_for_replacement.clear();

            //Initializing NodeAsPlacementDest nodes
            for (const ServerThread &st : global_hash_rings[Tier::MEMORY].get_unique_servers()) {

                // If does not exist
                if (host_statuses.find(st.public_ip()) == host_statuses.end()) {
                    NodeAsPlacementDest node;
                    node.st = st;
                    node.capacity = kMemoryNodeCapacity;
                    node.load = 0;
                    host_statuses[st.public_ip()] = node;
                }
            }

        }

        // Distibute Key_Replication_Map Updates
        update_end = std::chrono::system_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(update_end - update_start).count() >=
            kKeysUpdateThreshold) {

            // Publish changes
            if (local_change_set.size() > 0) {

                UpdateKeyMessage update_message;
                update_message.set_iter(update_iter);

                for (const Key &key : local_change_set) {

                    UpdateKeyTuple *tp = update_message.add_key_updates();
                    tp->set_key(key);
                    tp->set_master(key_replication_map[key].master_address_);
                    for (const auto &slave : key_replication_map[key].slave_addresses_) {
                        tp->add_slaves(slave);
                    }
                }


                // Send to the KVSes
                for (const ServerThread &st : global_hash_rings[Tier::MEMORY].get_unique_servers()) {
                    log->info("DEBUG: Send <UpdateMessage> to KVS {}", st.update_connect_address());
                    string serialized_req;
                    update_message.SerializeToString(&serialized_req);
                    kZmqUtil->send_string(serialized_req, &pushers[st.update_connect_address()]);
                }

                // Send to the Route servers
                for (const auto route_ip : routing_ips) {

                    UpdateKeyMessage update_message_for_route;
                    update_message_for_route.set_iter(update_iter);

                    for (const Key &key : local_change_set) {

                        UpdateKeyTuple *tp = update_message_for_route.add_key_updates();
                        tp->set_key(key);
                        tp->set_master(key_replication_map[key].master_address_);
                        Address min_delay_address;
                        auto min_delay = 10000;
                        for (const auto &slave : key_replication_map[key].slave_addresses_) {

                            // init min delay
                            if (min_delay == 10000) {
                                min_delay = delay_matrix[route_ip][slave];
                                min_delay_address = slave;
                            }

                                // check if this delay is less than the min delay
                            else {
                                if (delay_matrix[route_ip][slave] <= min_delay) {
                                    min_delay = delay_matrix[route_ip][slave];
                                    min_delay_address = slave;
                                }
                            }
                        }
                        tp->add_slaves(min_delay_address);
                    }

                    string serialized_req;
                    update_message_for_route.SerializeToString(&serialized_req);
                    // FIXME:
                    Address route_connect_address = "tcp://" + route_ip + ":" + std::to_string(kRouteUpdatePort);
                    log->info("DEBUG: Send <UpdateMessage> to ROUTE {}", route_connect_address);
                    kZmqUtil->send_string(serialized_req, &pushers[route_connect_address]);
                }


                update_iter++;
                local_change_set.clear();
            }

            update_start = std::chrono::system_clock::now();
        }

    }
}

