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

void replication_response_handler(
        unsigned &seed, unsigned &access_count, logger log, string &serialized,
        GlobalRingMap &global_hash_rings, LocalRingMap &local_hash_rings,
        map<Key, vector<PendingRequest>> &pending_requests,
        map<Key, vector<PendingGossip>> &pending_gossip,
        map<Key, std::multiset<TimePoint>> &key_access_tracker,
        map<Key, map<Address, std::multiset<TimePoint>>> &key_get_access_tracker,
        map<Key, map<Address, std::multiset<TimePoint>>> &key_put_access_tracker,
        map<Key, KeyProperty> &stored_key_map,
        map<Key, KeyReplication> &key_replication_map, set<Key> &local_changeset,
        ServerThread &wt, SerializerMap &serializers, SocketCache &pushers, const Address& local_address = "Not Set2") {
    KeyResponse response;
    response.ParseFromString(serialized);

    // we assume tuple 0 because there should only be one tuple responding to a
    // replication factor request
    KeyTuple tuple = response.tuples(0);
    Key key = get_key_from_metadata(tuple.key());

    //std::cout << "SERVER: GET KeyResponse message for key '" + key + "'\n";

    log->info("DEBUG: ---> KVS rep_resp_handler:\t Receive KeyResponse ID: {}, KEY: {}",
              response.response_id(), get_key_from_metadata(tuple.key()));

    log->info("DEBUG: Metadata key: {}", tuple.key());
    log->info("DEBUG: Key: {}", key);

    AnnaError error = tuple.error();

    if (error == AnnaError::NO_ERROR) {
        log->info("DEBUG: Checkpoint 1 - There was no error!");
        LWWValue lww_value;
        lww_value.ParseFromString(tuple.payload());
        ReplicationFactor rep_data;
        rep_data.ParseFromString(lww_value.value());

        for (const auto &global : rep_data.global()) {
            log->info("Set the global replication factor for key '{}' to '{}'", key, global.value());
            key_replication_map[key].global_replication_[global.tier()] =
                    global.value();
        }

        for (const auto &local : rep_data.local()) {
            key_replication_map[key].local_replication_[local.tier()] = local.value();
        }
    }
    else if (error == AnnaError::KEY_DNE) {
        // std::cout << "SERVER: KEY_DNE error happened for key '" + key + "'\n";
        log->info(
                "DEBUG: Checkpoint 2 -KEY_DNE means that the receiving thread was responsible for the metadata but didn't have any values stored");
        // KEY_DNE means that the receiving thread was responsible for the metadata
        // but didn't have any values stored -- we use the default rep factor
        init_replication(key_replication_map, key);
        // std::cout << "SERVER: Save key '" + key + " to key_replication_map of server'\n";
    }
    else if (error == AnnaError::WRONG_THREAD) {
        log->info(
                "DEBUG: Checkpoint 3 - means that the node that received the rep factor request was not\\"
                " responsible for that metadata");
        // this means that the node that received the rep factor request was not
        // responsible for that metadata
        auto respond_address = wt.replication_response_connect_address();
        kHashRingUtil->issue_replication_factor_request(
                respond_address, key, global_hash_rings[Tier::MEMORY],
                local_hash_rings[Tier::MEMORY], pushers, seed);
        return;
    }
    else {
        log->error("Unexpected error type {} in replication factor response.",
                   error);
        return;
    }

    bool succeed;

    // If the key is in the pending requests
    if (pending_requests.find(key) != pending_requests.end()) {

        // std::cout << "SERVER: The key '" + key + " is among the pending requests.'\n";

        log->info("DEBUG: Checkpoint 4 - If the key is in the pending requests");
        log->info("Get thread for key: {}", key);
        ServerThreadList threads = kHashRingUtil->get_responsible_threads(
                wt.replication_response_connect_address(), key, is_metadata(key),
                global_hash_rings, local_hash_rings, key_replication_map, pushers,
                kSelfTierIdVector, succeed, seed);

        if (succeed) {

            log->info("thread request was successfull");
            bool responsible =
                    std::find(threads.begin(), threads.end(), wt) != threads.end();

            for (const PendingRequest &request : pending_requests[key]) {
                auto now = std::chrono::system_clock::now();

                // No responsible thread exist and not response address
                if (!responsible && request.addr_ != "") {
                    log->info("No responsible thread exist and not response address");

                    KeyResponse response;

                    response.set_type(request.type_);

                    if (request.response_id_ != "") {
                        response.set_response_id(request.response_id_);
                    }

                    KeyTuple *tp = response.add_tuples();
                    tp->set_key(key);
                    tp->set_error(AnnaError::WRONG_THREAD);

                    string serialized_response;
                    response.SerializeToString(&serialized_response);
                    kZmqUtil->send_string(serialized_response, &pushers[request.addr_]);
                }
                    // Responsible thread exist but not response address
                else if (responsible && request.addr_ == "") {
                    log->info("Responsible thread exist but not response address");
                    // only put requests should fall into this category
                    if (request.type_ == RequestType::PUT) {
                        if (request.lattice_type_ == LatticeType::NONE) {
                            log->error("PUT request missing lattice type.");
                        }
                        else if (stored_key_map.find(key) != stored_key_map.end() &&
                                   stored_key_map[key].type_ != LatticeType::NONE &&
                                   stored_key_map[key].type_ != request.lattice_type_) {

                            log->error(
                                    "Lattice type mismatch for key {}: query is {} but we expect "
                                    "{}.",
                                    key, LatticeType_Name(request.lattice_type_),
                                    LatticeType_Name(stored_key_map[key].type_));
                        }
                        else {
                            process_put(key, request.lattice_type_, request.payload_,
                                        serializers[request.lattice_type_], stored_key_map);

                            // key_access_tracker[key].insert(now);
                            key_put_access_tracker[key]["not known"].insert(now);
                            access_count += 1;
                            local_changeset.insert(key);
                        }
                    } else {
                        log->error("Received a GET request with no response address.");
                    }
                }
                    // Responsible thread and response address exist
                else if (responsible && request.addr_ != "") {
                    log->info("Responsible thread and response address exist");
                    KeyResponse response;

                    response.set_type(request.type_);

                    if (request.response_id_ != "") {
                        response.set_response_id(request.response_id_);
                    }

                    KeyTuple *tp = response.add_tuples();
                    tp->set_key(key);

                    if (request.type_ == RequestType::GET) {
                        log->info("It's a GET");
                        if (stored_key_map.find(key) == stored_key_map.end() ||
                            stored_key_map[key].type_ == LatticeType::NONE) {
                            tp->set_error(AnnaError::KEY_DNE);
                        } else {
                            auto res =
                                    process_get(key, serializers[stored_key_map[key].type_]);
                            tp->set_lattice_type(stored_key_map[key].type_);
                            tp->set_payload(res.first);
                            tp->set_error(res.second);
                        }
                    }
                    else {
                        log->info("It's not a GET");
                        if (request.lattice_type_ == LatticeType::NONE) {
                            log->error("PUT request missing lattice type.");
                        }
                        else if (stored_key_map.find(key) != stored_key_map.end() &&
                                   stored_key_map[key].type_ != LatticeType::NONE &&
                                   stored_key_map[key].type_ != request.lattice_type_) {
                            log->error(
                                    "Lattice type mismatch for key {}: {} from query but {} "
                                    "expected.",
                                    key, LatticeType_Name(request.lattice_type_),
                                    LatticeType_Name(stored_key_map[key].type_));
                        }
                        else {
                            log->info("Processing put finally of key '{}'", key);
                            process_put(key, request.lattice_type_, request.payload_,
                                        serializers[request.lattice_type_], stored_key_map, true);
                            key_replication_map[key].master_address_ = local_address;
                            //////////////////////////////////////////////////////////////////

                            string master_meta_key = "ANNA_METADATA|" + key + "|master";
                            ServerThreadList threads_for_meta = kHashRingUtil->get_responsible_threads(
                                    wt.replication_response_connect_address(), master_meta_key,
                                    is_metadata(master_meta_key),
                                    global_hash_rings, local_hash_rings, key_replication_map, pushers,
                                    kSelfTierIdVector, succeed, seed);
                            if (succeed) {
                                if (std::find(threads.begin(), threads.end(), wt) == threads.end()) {

                                    if (is_metadata(key)) {
                                        // this means that this node is not responsible for this metadata key
                                        KeyTuple *tp = response.add_tuples();

                                        tp->set_key(master_meta_key);
                                        tp->set_lattice_type(tuple.lattice_type());
                                        tp->set_error(AnnaError::WRONG_THREAD);
                                    } else {
                                        // FIXME: For debug purpose
                                        log->info("DEBUG: ERROR");
                                    }
                                } else { // if we know the responsible threads, we process the request
                                    KeyTuple *tp = response.add_tuples();
                                    tp->set_key(master_meta_key);

                                    process_put(master_meta_key, request.lattice_type_,
                                                request.payload_,
                                                serializers[request.lattice_type_], stored_key_map);

                                    local_changeset.insert(master_meta_key);
                                    tp->set_lattice_type(request.lattice_type_);

                                }
                            } else {
                                // FIXME: For debug purpose
                                log->info("DEBUG: ERROR");
                            }

                            //////////////////////////////////////////////////////////////////
                            tp->set_lattice_type(request.lattice_type_);
                            local_changeset.insert(key);
                        }
                    }

                    if (request.type_ == RequestType::GET){
                        key_get_access_tracker[key][request.addr_].insert(now);
                    }
                    else{
                        key_put_access_tracker[key][request.addr_].insert(now);
                    }
                    access_count += 1;

                    string serialized_response;
                    response.SerializeToString(&serialized_response);
                    kZmqUtil->send_string(serialized_response, &pushers[request.addr_]);
                }
            }
        } else {
            log->error(
                    "Missing key replication factor in process pending request routine.");
        }

        pending_requests.erase(key);
    }

    // If the key is in the pending gossips
    if (pending_gossip.find(key) != pending_gossip.end()) {

        // std::cout << "SERVER: The key '" + key + " is among the gossips.'\n";

        log->info("DEBUG: Checkpoint 5 - If the key is in the pending gossips");
        ServerThreadList threads = kHashRingUtil->get_responsible_threads(
                wt.replication_response_connect_address(), key, is_metadata(key),
                global_hash_rings, local_hash_rings, key_replication_map, pushers,
                kSelfTierIdVector, succeed, seed);

        if (succeed) {
            if (std::find(threads.begin(), threads.end(), wt) != threads.end()) {
                for (const PendingGossip &gossip : pending_gossip[key]) {
                    if (stored_key_map.find(key) != stored_key_map.end() &&
                        stored_key_map[key].type_ != LatticeType::NONE &&
                        stored_key_map[key].type_ != gossip.lattice_type_) {
                        log->error("Lattice type mismatch for key {}: {} from query but {} "
                                   "expected.",
                                   key, LatticeType_Name(gossip.lattice_type_),
                                   LatticeType_Name(stored_key_map[key].type_));
                    } else {
                        log->info("DEBUG: Master address of the gossiped key '{}' is {}", key, gossip.master_);
                        process_put(key, gossip.lattice_type_, gossip.payload_,
                                    serializers[gossip.lattice_type_], stored_key_map, false);
                        key_replication_map[key].master_address_ = gossip.master_;
                    }
                }
            } else {
                map<Address, KeyRequest> gossip_map;

                // forward the gossip
                for (const ServerThread &thread : threads) {
                    gossip_map[thread.gossip_connect_address()].set_type(
                            RequestType::PUT);

                    for (const PendingGossip &gossip : pending_gossip[key]) {
                        prepare_put_tuple(gossip_map[thread.gossip_connect_address()], key,
                                          gossip.lattice_type_, gossip.payload_);
                    }
                }

                // redirect gossip
                for (const auto &gossip_pair : gossip_map) {
                    string serialized;
                    gossip_pair.second.SerializeToString(&serialized);
                    kZmqUtil->send_string(serialized, &pushers[gossip_pair.first]);
                }
            }
        } else {
            log->error(
                    "Missing key replication factor in process pending gossip routine.");
        }

        pending_gossip.erase(key);
    }
}
