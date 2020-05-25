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

#include "route/routing_handlers.hpp"
#include "yaml-cpp/yaml.h"

hmap<Tier, TierMetadata, TierEnumHash> kTierMetadata;
unsigned kDefaultLocalReplication;
unsigned kRoutingThreadCount;

unsigned kMemoryNodeCapacity;
unsigned kEbsNodeCapacity;

ZmqUtil zmq_util;
ZmqUtilInterface *kZmqUtil = &zmq_util;

HashRingUtil hash_ring_util;
HashRingUtilInterface *kHashRingUtil = &hash_ring_util;

void run(unsigned thread_id, Address ip, vector<Address> monitoring_ips) {
    string log_file = "log_routing_" + std::to_string(thread_id) + ".txt";
    string log_name = "routing_log_" + std::to_string(thread_id);
    auto log = spdlog::basic_logger_mt(log_name, log_file, true);
    log->flush_on(spdlog::level::info);


    RoutingThread rt = RoutingThread(ip, thread_id);

    unsigned seed = time(NULL);
    seed += thread_id;

    // prepare the zmq context
    zmq::context_t context(1);
    SocketCache pushers(&context, ZMQ_PUSH);
    map<Key, KeyReplication> key_replication_map;

    if (thread_id == 0) {
        // notify monitoring nodes
        for (const string &address : monitoring_ips) {
            kZmqUtil->send_string(
                    // add null because it expects two IPs from server nodes...
                    "join:" + Tier_Name(Tier::ROUTING) + ":" + ip + ":NULL",
                    &pushers[MonitoringThread(address).notify_connect_address()]);
        }
    }

    // initialize hash ring maps
    GlobalRingMap global_hash_rings;
    LocalRingMap local_hash_rings;

    // pending events for asynchrony
    map<Key, vector<pair<Address, string>>> pending_requests;

    // form local hash rings
    for (const auto &pair : kTierMetadata) {
        TierMetadata tier = pair.second;
        for (unsigned tid = 0; tid < tier.thread_number_; tid++) {
            local_hash_rings[tier.id_].insert(ip, ip, 0, tid);
        }
    }

    // responsible for sending existing server addresses to a new node (relevant
    // to seed node)
    zmq::socket_t addr_responder(context, ZMQ_REP);
    addr_responder.bind(rt.seed_bind_address());

    // responsible for both node join and departure
    zmq::socket_t notify_puller(context, ZMQ_PULL);
    notify_puller.bind(rt.notify_bind_address());

    // responsible for listening for key replication factor response
    zmq::socket_t replication_response_puller(context, ZMQ_PULL);
    replication_response_puller.bind(rt.replication_response_bind_address());

    // responsible for handling key replication factor change requests from server
    // nodes
    zmq::socket_t replication_change_puller(context, ZMQ_PULL);
    replication_change_puller.bind(rt.replication_change_bind_address());

    // responsible for handling key address request from users
    zmq::socket_t key_address_puller(context, ZMQ_PULL);
    key_address_puller.bind(rt.key_address_bind_address());

    //responsible for receiving KeyResponse messages and handle them
    zmq::socket_t response_puller_(context, ZMQ_PULL);
    response_puller_.bind(rt.response_bind_address());

    zmq::socket_t update_puller_(context, ZMQ_PULL);
    update_puller_.bind(rt.update_bind_address());

    vector<zmq::pollitem_t> pollitems = {
            {static_cast<void *>(addr_responder),              0, ZMQ_POLLIN, 0},
            {static_cast<void *>(notify_puller),               0, ZMQ_POLLIN, 0},
            {static_cast<void *>(replication_response_puller), 0, ZMQ_POLLIN, 0},
            {static_cast<void *>(replication_change_puller),   0, ZMQ_POLLIN, 0},
            {static_cast<void *>(key_address_puller),          0, ZMQ_POLLIN, 0},
            {static_cast<void *>(response_puller_),            0, ZMQ_POLLIN, 0},
            {static_cast<void *>(update_puller_),            0, ZMQ_POLLIN, 0}};

    auto report_start = std::chrono::system_clock::now();
    auto report_end = std::chrono::system_clock::now();

    while (true) {
        //kZmqUtil->poll(-1, &pollitems);
        kZmqUtil->poll(0, &pollitems);

        // only relavant for the seed node
        if (pollitems[0].revents & ZMQ_POLLIN) {
            kZmqUtil->recv_string(&addr_responder);
            auto serialized = seed_handler(log, global_hash_rings);
            //log->info("DEBUG: GET ADDR RESPONDER: {}", serialized);
            kZmqUtil->send_string(serialized, &addr_responder);
        }

        // handle a join or depart event coming from the server side
        if (pollitems[1].revents & ZMQ_POLLIN) {

            string serialized = kZmqUtil->recv_string(&notify_puller);
            //log->info("DEBUG: GET NOTIFY - handle a join or depart event coming from the server side: {}", serialized);
            membership_handler(log, serialized, pushers, global_hash_rings, thread_id,
                               ip);
        }

        // received replication factor response
        if (pollitems[2].revents & ZMQ_POLLIN) {

            string serialized = kZmqUtil->recv_string(&replication_response_puller);
            //log->info("DEBUG: GET REPLICATION FACTOR RESPONSE: {}", serialized);
            replication_response_handler(log, serialized, pushers, rt,
                                         global_hash_rings, local_hash_rings,
                                         key_replication_map, pending_requests, seed);
        }

        if (pollitems[3].revents & ZMQ_POLLIN) {

            string serialized = kZmqUtil->recv_string(&replication_change_puller);
            //log->info("DEBUG: GET REPLICATION CHANGE RESPONSE: {}", serialized);
            replication_change_handler(log, serialized, pushers, key_replication_map,
                                       thread_id, ip);
        }

        if (pollitems[4].revents & ZMQ_POLLIN) {
            string serialized = kZmqUtil->recv_string(&key_address_puller);
            //log->info("DEBUG: GET KEY ADDRESS RESPONSE: {}", serialized);
            address_handler(log, serialized, pushers, rt, global_hash_rings,
                            local_hash_rings, key_replication_map, pending_requests,
                            seed, ip, monitoring_ips[0]);
        }

        if (pollitems[5].revents & ZMQ_POLLIN) {
            string serialized = kZmqUtil->recv_string(&response_puller_);
            //log->info("DEBUG: ---> ROUTE routeing.cpp:\tIIIIGENNNNN Receive KeyResponse ID");
            KeyResponse response;
            response.ParseFromString(serialized);

            // we assume tuple 0 because there should only be one tuple responding to a
            // replication factor request
            KeyTuple tuple = response.tuples(0);
            Key metadata_master_key = tuple.key();
            vector<string> v;
            split(metadata_master_key, '|', v);
            Key key = v[1];
            key_replication_map[key].master_address_ = tuple.payload();
            log->info("DEBUG: The master address of key '{}' is {}", key, key_replication_map[key].master_address_);
        }

        if (pollitems[6].revents & ZMQ_POLLIN){

            string serialized = kZmqUtil->recv_string(&update_puller_);
            UpdateKeyMessage update_message;
            update_message.ParseFromString(serialized);

            for (const auto &key_update : update_message.key_updates()) {
                Key key = key_update.key();
                Address master = key_update.master();
                key_replication_map[key].master_address_ = master;
                key_replication_map[key].slave_addresses_.clear();
                for (const auto &slave : key_update.slaves()) {
                    key_replication_map[key].slave_addresses_.insert(slave);
                }
            }
        }



        report_end = std::chrono::system_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::seconds>(
                report_end - report_start)
                .count();
        if (duration >= 10) {
            /////////////////////////////////////////////////////////////////////////
            log->info("DEBUG: (STAT) ---------------------------------------------------");
            log->info("DEBUG: (STAT) Content of the key_replication_map");
            for (const auto &key_rep : key_replication_map) {
                if (key_rep.first.find("METADATA") == std::string::npos) {
                    log->info("DEBUG: (STAT) \t - Key: '{}'\t Master address: '{}'\tSlave addresses:", key_rep.first, key_rep.second.master_address_);
                    for (const auto &slave : key_rep.second.slave_addresses_) {
                        log->info("DEBUG: (STAT) \t\t\t\t\t\t *  {}", slave);
                    }
                }
            }
            log->info("DEBUG: (STAT) ---------------------------------------------------");
            log->info("\n");
            /////////////////////////////////////////////////////////////////////////
            report_start = std::chrono::system_clock::now();
        }

    }
}

int main(int argc, char *argv[]) {
    if (argc != 1) {
        std::cerr << "Usage: " << argv[0] << std::endl;
        return 1;
    }

    YAML::Node conf = YAML::LoadFile("conf/anna-config.yml");
    YAML::Node threads = conf["threads"];
    unsigned kMemoryThreadCount = threads["memory"].as<unsigned>();
    unsigned kEbsThreadCount = threads["ebs"].as<unsigned>();
    kRoutingThreadCount = threads["routing"].as<unsigned>();

    YAML::Node capacities = conf["capacities"];
    kMemoryNodeCapacity = capacities["memory-cap"].as<unsigned>() * 1000000;
    kEbsNodeCapacity = capacities["ebs-cap"].as<unsigned>() * 1000000;

    YAML::Node replication = conf["replication"];
    unsigned kDefaultGlobalMemoryReplication =
            replication["memory"].as<unsigned>();
    unsigned kDefaultGlobalEbsReplication = replication["ebs"].as<unsigned>();
    kDefaultLocalReplication = replication["local"].as<unsigned>();

    YAML::Node routing = conf["routing"];
    Address ip = routing["ip"].as<string>();
    vector<Address> monitoring_ips;

    for (const YAML::Node &node : routing["monitoring"]) {
        string address = node.as<Address>();
        monitoring_ips.push_back(address);
    }

    kTierMetadata[Tier::MEMORY] =
            TierMetadata(Tier::MEMORY, kMemoryThreadCount,
                         kDefaultGlobalMemoryReplication, kMemoryNodeCapacity);
    kTierMetadata[Tier::DISK] =
            TierMetadata(Tier::DISK, kEbsThreadCount, kDefaultGlobalEbsReplication,
                         kEbsNodeCapacity);

    vector<std::thread> routing_worker_threads;

    for (unsigned thread_id = 1; thread_id < kRoutingThreadCount; thread_id++) {
        routing_worker_threads.push_back(
                std::thread(run, thread_id, ip, monitoring_ips));
    }

    run(0, ip, monitoring_ips);
}
