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
#include "kvs/kvs_handlers.hpp"

// the current update id
unsigned update_id_ = 0;

void user_request_handler(
        unsigned &access_count, unsigned &seed, string &serialized, logger log,
        GlobalRingMap &global_hash_rings, LocalRingMap &local_hash_rings,
        map<Key, vector<PendingRequest>> &pending_requests,
        map<Key, std::multiset<TimePoint>> &key_access_tracker,
        map<Key, map<Address, std::multiset<TimePoint>>> &key_get_access_tracker,
        map<Key, map<Address, std::multiset<TimePoint>>> &key_put_access_tracker,
        map<Key, KeyProperty> &stored_key_map,
        map<Key, KeyReplication> &key_replication_map, set<Key> &local_changeset,
        ServerThread &wt, SerializerMap &serializers, SocketCache &pushers, vector<Address> monitoring_ips) {
    KeyRequest request;
    request.ParseFromString(serialized);

    //std::cout << "SERVER: user_request_handler\n";
    //log->info("DEBUG: \t Receive KeyRequest ID: {}, FROM : {}, KEY: {}",
    //          request.request_id(), request.response_address(), request.tuples()[0].key());


    KeyResponse response;
    string response_id = request.request_id();
    response.set_response_id(request.request_id());

    response.set_type(request.type());

    bool succeed;
    RequestType request_type = request.type();
    string response_address = request.response_address();

    //FIXME: Assume that only one tuple is in the request
    Key main_key;
    for (const auto &tuple : request.tuples()) {
        // first check if the thread is responsible for the key
        Key key = tuple.key();
        main_key = key;
        string payload = tuple.payload();

        // If the requested key is not a METADATA
        if (key.find("METADATA") == std::string::npos) {
            //log->info("DEBUG: KV Server get the request related to the key {}. ", key);

            // DEL request
            if (request_type == RequestType::DEL) {

                //log->info("DEBUG: Request is to DEL key '{}'*****", key);
                KeyTuple *tp = response.add_tuples();
                tp->set_key(key);

                if (stored_key_map.find(key) != stored_key_map.end()) {

                    //log->info("It's stored...");

                    if (key_replication_map[key].master_address_ == wt.public_ip()) {
                        key_replication_map[key].master_address_ = "";

                    }
                    key_replication_map[key].slave_addresses_.clear();
                    process_del(key, serializers[tuple.lattice_type()], stored_key_map);
                    /*
                    log->info("###########################################################################");
                    log->info("DEBUG: (STAT) Content of the key_replication_map:");
                    for (const auto &key_rep : key_replication_map) {
                        if (key_rep.first.find("METADATA") == std::string::npos) {
                            log->info("DEBUG: (STAT) \t - Key: '{}'\t Master address: '{}'\tSlave addresses:", key_rep.first,
                                      key_rep.second.master_address_);
                            for (const auto &slave : key_rep.second.slave_addresses_) {
                                log->info("DEBUG: (STAT) \t\t\t\t\t\t *  {}", slave);
                            }
                        }
                    }
                    log->info("###########################################################################");
                     */
                }
                if (key_replication_map.find(key) != key_replication_map.end()) {
                    //log->info("It's within the key_replication_map...");
                    if (key_replication_map[key].master_address_ == wt.public_ip()) {
                        key_replication_map[key].master_address_ = "";

                    }
                    key_replication_map[key].slave_addresses_.clear();
                    /*
                    log->info("###########################################################################");
                    log->info("DEBUG: (STAT) Content of the key_replication_map:");
                    for (const auto &key_rep : key_replication_map) {
                        if (key_rep.first.find("METADATA") == std::string::npos) {
                            log->info("DEBUG: (STAT) \t - Key: '{}'\t Master address: '{}'\tSlave addresses:", key_rep.first,
                                      key_rep.second.master_address_);
                            for (const auto &slave : key_rep.second.slave_addresses_) {
                                log->info("DEBUG: (STAT) \t\t\t\t\t\t *  {}", slave);
                            }
                        }
                    }
                    log->info("###########################################################################");
                     */
                }
            }

                // PUT request
            else if (request_type == RequestType::PUT) {
                // If this server contains the master replica of the requested key
                if (key_replication_map[key].master_address_ == wt.public_ip()) {
                    //log->info("DEBUG: Request is to PUT key '{}'", key);
                    succeed = true;

                    KeyTuple *tp = response.add_tuples();
                    tp->set_key(key);

                    if (tuple.lattice_type() == LatticeType::NONE) {
                        log->error("PUT request missing lattice type.");
                    }
                        // if the requested key does not exist in the KV
                    else if (stored_key_map.find(key) != stored_key_map.end() &&
                             stored_key_map[key].type_ != LatticeType::NONE &&
                             stored_key_map[key].type_ != tuple.lattice_type()) {
                        log->error(
                                "Lattice type mismatch for key {}: query is {} but we expect "
                                "{}.",
                                key, LatticeType_Name(tuple.lattice_type()),
                                LatticeType_Name(stored_key_map[key].type_));
                    } else {
                        process_put(key, tuple.lattice_type(), payload,
                                    serializers[tuple.lattice_type()], stored_key_map);
                        local_changeset.insert(key);
                        tp->set_lattice_type(tuple.lattice_type());

                        // If the key has slave replicas, they need to be updated
                        for (const auto &slave : key_replication_map[key].slave_addresses_) {
                            for (const ServerThread &servert : global_hash_rings[Tier::MEMORY].get_unique_servers()) {
                                if (servert.public_ip() == slave and servert.public_ip() != wt.public_ip()) {

                                    //log->info("SERVER: Send out <SlaveUpdate> message for key '{}'",key);

                                    KeyRequest slave_update_request;

                                    slave_update_request.set_type(RequestType::UPDATE);
                                    slave_update_request.set_response_address(request.response_address());

                                    // generating ID
                                    if (++update_id_ % 10000 == 0) update_id_ = 0;
                                    auto request_id =
                                            wt.public_ip() + ":" + std::to_string(wt.tid()) + "_" +
                                            std::to_string(update_id_++);
                                    slave_update_request.set_request_id(request_id);

                                    KeyTuple *tp = slave_update_request.add_tuples();
                                    tp->set_key(key);
                                    tp->set_lattice_type(tuple.lattice_type());
                                    tp->set_payload(payload);
                                    tp->set_master(key_replication_map[key].master_address_);

                                    // set target address
                                    Address target_address = servert.slave_update_connect_address();

                                    // sending out the request
                                    string serialized_req;
                                    slave_update_request.SerializeToString(&serialized_req);
                                    kZmqUtil->send_string(serialized_req, &pushers[target_address]);
                                    //log->info("SERVER: SENDING out <SlaveUpdate> message for key {} to {}", key,
                                    //          target_address);
                                    break;
                                }
                            }
                        }
                    }

                    // Set the access statistics
                    vector<string> tokens;
                    split(request.response_address(), ':', tokens);
                    auto address = tokens[1].erase(0, 2);

                    key_put_access_tracker[key][address].insert(std::chrono::system_clock::now());
                    access_count += 1;

                }
                    // If not this server is repsonsible for the key, i.e., the master replica does not be stored here
                else {
                    log->error("PUT request failed: not this server is the key {}'s master", key);

                    //log->info("DEBUG: Send KeyResponse to {}, id: {}, key: {}", request.response_address(),
                    //         response.response_id(), request.tuples()[0].key());
                    KeyTuple *tp = response.add_tuples();
                    tp->set_key(key);
                    tp->set_invalidate(true);
                    tp->set_error(AnnaError::WRONG_THREAD);
                    string serialized_response;
                    response.SerializeToString(&serialized_response);
                    kZmqUtil->send_string(serialized_response,
                                          &pushers[request.response_address()]);

                    /*
                    KeyAddressResponse addr_response;
                    addr_response.set_response_id(request.request_id());
                    KeyAddressResponse_KeyAddress *tp = addr_response.add_addresses();
                    tp->set_key(key);
                    Address monitor_ip;
                    for(const auto ip : monitoring_ips){
                        monitor_ip = ip;
                        break;
                    }
                    tp->add_ips(MonitoringThread(monitor_ip).key_request_connect_address());
                    tp->set_request_type(RequestType::PUT);
                    //FIXME:
                    vector<string> tokens;
                    split(request.response_address(), ':', tokens);
                    auto response_ip_address = tokens[1].erase(0, 2);
                    auto response_addr = "tcp://" + response_ip_address + ":16850";

                    log->info("ROUTE: SENDING out <KeyAddressResponse> to {}", response_addr);
                    string serialized2;
                    addr_response.SerializeToString(&serialized2);

                    kZmqUtil->send_string(serialized2, &pushers[response_addr]);
                     */
                }
            }

                // GET request
            else if (request_type == RequestType::GET) {
                // If this server contains either the master or slave replica of the requested key
                if (key_replication_map[key].master_address_ == wt.public_ip() ||
                    key_replication_map[key].slave_addresses_.find(wt.public_ip()) !=
                    key_replication_map[key].slave_addresses_.end()) {
                    //log->info("DEBUG: Request is to GET key '{}'", key);
                    succeed = true;

                    KeyTuple *tp = response.add_tuples();
                    tp->set_key(key);

                    // FIXME: I'm not sure if that is good
                    auto res = process_get(key, serializers[stored_key_map[key].type_]);
                    tp->set_lattice_type(stored_key_map[key].type_);
                    tp->set_payload(res.first);
                    tp->set_error(res.second);

                    // Set the access statistics
                    vector<string> tokens;
                    split(request.response_address(), ':', tokens);
                    auto address = tokens[1].erase(0, 2);

                    key_get_access_tracker[key][address].insert(std::chrono::system_clock::now());
                    access_count += 1;

                }

                    // If not this server is repsonsible for the key
                else {
                    log->error("GET request failed: this server does not contain key {}", key);

                    //log->info("DEBUG: Send <KeyResponse> to {}, id: {}, key: {}", request.response_address(),
                    //          response.response_id(), request.tuples()[0].key());
                    KeyTuple *tp = response.add_tuples();
                    tp->set_key(key);
                    tp->set_invalidate(true);
                    tp->set_error(AnnaError::WRONG_THREAD);

                    string serialized_response;
                    response.SerializeToString(&serialized_response);
                    kZmqUtil->send_string(serialized_response,
                                          &pushers[request.response_address()]);

                    /*
                    KeyAddressResponse addr_response;
                    addr_response.set_response_id(request.request_id());
                    KeyAddressResponse_KeyAddress *tp = addr_response.add_addresses();
                    tp->set_key(key);

                    Address monitor_ip;
                    for(const auto ip : monitoring_ips){
                        monitor_ip = ip;
                        break;
                    }
                    tp->add_ips(MonitoringThread(monitor_ip).key_request_connect_address());
                    tp->set_request_type(RequestType::GET);
                    //FIXME:
                    vector<string> tokens;
                    split(request.response_address(), ':', tokens);
                    auto response_ip_address = tokens[1].erase(0, 2);
                    auto response_addr = "tcp://" + response_ip_address + ":16850";

                    log->info("ROUTE: SENDING out <KeyAddressResponse> to {}", response_addr);
                    string serialized2;
                    addr_response.SerializeToString(&serialized2);

                    kZmqUtil->send_string(serialized2, &pushers[response_addr]);
                     */
                }

            }

                // Other request
            else {
                log->error("Unknown request type {} in user request handler.",
                           request_type);
            }

        }

            // If the key is an AnnaDB METADATA
        else if (is_metadata(key)) {

            //log->info("METADATA '{}' GET/PUT request has arrived", key);

            // Then we serve the request according to the built in hash ring placement method

            ServerThreadList threads = kHashRingUtil->get_responsible_threads(
                    wt.replication_response_connect_address(), key, is_metadata(key),
                    global_hash_rings, local_hash_rings, key_replication_map, pushers,
                    kSelfTierIdVector, succeed, seed);

            if (succeed) {
                if (std::find(threads.begin(), threads.end(), wt) == threads.end()) {
                    if (is_metadata(key)) {
                        // this means that this node is not responsible for this metadata key
                        KeyTuple *tp = response.add_tuples();

                        tp->set_key(key);
                        tp->set_lattice_type(tuple.lattice_type());
                        tp->set_error(AnnaError::WRONG_THREAD);
                    } else {
                        // if we don't know what threads are responsible, we issue a rep
                        // factor request and make the request pending
                        kHashRingUtil->issue_replication_factor_request(
                                wt.replication_response_connect_address(), key,
                                global_hash_rings[Tier::MEMORY], local_hash_rings[Tier::MEMORY],
                                pushers, seed);

                        pending_requests[key].push_back(
                                PendingRequest(request_type, tuple.lattice_type(), payload,
                                               response_address, response_id));
                    }
                } else { // if we know the responsible threads, we process the request
                    KeyTuple *tp = response.add_tuples();
                    tp->set_key(key);

                    if (request_type == RequestType::GET) {
                        if (stored_key_map.find(key) == stored_key_map.end() ||
                            stored_key_map[key].type_ == LatticeType::NONE) {

                            tp->set_error(AnnaError::KEY_DNE);
                        } else {
                            auto res = process_get(key, serializers[stored_key_map[key].type_]);
                            tp->set_lattice_type(stored_key_map[key].type_);
                            tp->set_payload(res.first);
                            tp->set_error(res.second);
                        }
                    } else if (request_type == RequestType::PUT) {
                        if (tuple.lattice_type() == LatticeType::NONE) {
                            log->error("PUT request missing lattice type.");
                        } else if (stored_key_map.find(key) != stored_key_map.end() &&
                                   stored_key_map[key].type_ != LatticeType::NONE &&
                                   stored_key_map[key].type_ != tuple.lattice_type()) {
                            log->error(
                                    "Lattice type mismatch for key {}: query is {} but we expect "
                                    "{}.",
                                    key, LatticeType_Name(tuple.lattice_type()),
                                    LatticeType_Name(stored_key_map[key].type_));
                        } else {
                            process_put(key, tuple.lattice_type(), payload,
                                        serializers[tuple.lattice_type()], stored_key_map);

                            local_changeset.insert(key);
                            tp->set_lattice_type(tuple.lattice_type());
                        }
                    } else {
                        log->error("Unknown request type {} in user request handler.",
                                   request_type);
                    }

                    if (tuple.address_cache_size() > 0 &&
                        tuple.address_cache_size() != threads.size()) {
                        tp->set_invalidate(true);
                    }

                    key_access_tracker[key].insert(std::chrono::system_clock::now());
                    access_count += 1;
                }
            } else {
                pending_requests[key].push_back(
                        PendingRequest(request_type, tuple.lattice_type(), payload,
                                       response_address, response_id));
            }

        }


    }

    if (response.tuples_size() > 0 && request.response_address() != "") {

        // FIXME: I'm not sure if we delete this, it would cause any kind of failure
        response.set_master_address(key_replication_map[main_key].master_address_);

        //std::cout << "SERVER: Send KeyResponse back " + request.response_address()+"'\n";

        //log->info("DEBUG: SENDING OUT KeyResponse, ID: {}",response_id);
        //log->info("DEBUG: Send <KeyResponse> to {}, id: {}, key: {}", request.response_address(),
        //          response.response_id(), request.tuples()[0].key());
        string serialized_response;
        response.SerializeToString(&serialized_response);
        kZmqUtil->send_string(serialized_response,
                              &pushers[request.response_address()]);
    }
}
