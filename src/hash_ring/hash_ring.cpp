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

#include "hash_ring.hpp"

#include <unistd.h>
#include <iostream>

#include "requests.hpp"


// get all threads responsible for a key from the "node_type" tier
// metadata flag = 0 means the key is  metadata; otherwise, it is  regular data
ServerThreadList HashRingUtil::get_responsible_threads(
        Address response_address, const Key &key, bool metadata,
        GlobalRingMap &global_hash_rings, LocalRingMap &local_hash_rings,
        map<Key, KeyReplication> &key_replication_map, SocketCache &pushers,
        const vector<Tier> &tiers, bool &succeed, unsigned &seed) {
    if (metadata) {
        succeed = true;
        return kHashRingUtil->get_responsible_threads_metadata(
                key, global_hash_rings[Tier::MEMORY], local_hash_rings[Tier::MEMORY]);
    }
        // if the requested key is not a metadata
    else {
        ServerThreadList result;

        // std::cout << "DEBUG: Quering the responsible servers where the key '" + key + "' is located.\n";
        if (key_replication_map.find(key) == key_replication_map.end()) {
            kHashRingUtil->issue_replication_factor_request(
                    response_address, key, global_hash_rings[Tier::MEMORY],
                    local_hash_rings[Tier::MEMORY], pushers, seed);
            succeed = false;
        } else {
            for (const Tier &tier : tiers) {
                ServerThreadList threads = responsible_global(
                        key, key_replication_map[key].global_replication_[tier],
                        global_hash_rings[tier]);

                for (const ServerThread &thread : threads) {
                    Address public_ip = thread.public_ip();
                    //std::cout << "DEBUG: \t - Address: '" + public_ip + "'\n";
                    Address private_ip = thread.private_ip();

                    set<unsigned> tids = responsible_local(
                            key, key_replication_map[key].local_replication_[tier],
                            local_hash_rings[tier]);


                    for (const unsigned &tid : tids) {
                        result.push_back(ServerThread(public_ip, private_ip, tid));
                    }
                }
            }

            succeed = true;
        }
        return result;
    }
}

// assuming the replication factor will never be greater than the number of
// nodes in a tier return a set of ServerThreads that are responsible for a key
ServerThreadList responsible_global(const Key &key, unsigned global_rep,
                                    GlobalHashRing &global_hash_ring) {
    ServerThreadList threads;
    auto pos = global_hash_ring.find(key);

    if (pos != global_hash_ring.end()) {
        // iterate for every value in the replication factor
        unsigned i = 0;

        while (i < global_rep) {
            if (std::find(threads.begin(), threads.end(), pos->second) ==
                threads.end()) {
                threads.push_back(pos->second);
                i += 1;
            }
            if (++pos == global_hash_ring.end()) {
                pos = global_hash_ring.begin();
            }
        }
    }

    return threads;
}

// assuming the replication factor will never be greater than the number of
// worker threads return a set of tids that are responsible for a key
set<unsigned> responsible_local(const Key &key, unsigned local_rep,
                                LocalHashRing &local_hash_ring) {
    set<unsigned> tids;
    auto pos = local_hash_ring.find(key);

    if (pos != local_hash_ring.end()) {
        // iterate for every value in the replication factor
        unsigned i = 0;

        while (i < local_rep) {
            bool succeed = tids.insert(pos->second.tid()).second;
            if (++pos == local_hash_ring.end()) {
                pos = local_hash_ring.begin();
            }

            if (succeed) {
                i += 1;
            }
        }
    }

    return tids;
}

Address prepare_metadata_request(const Key &key,
                                 GlobalHashRing &global_memory_hash_ring,
                                 LocalHashRing &local_memory_hash_ring,
                                 map<Address, KeyRequest> &addr_request_map,
                                 Address response_address, unsigned &rid,
                                 RequestType type) {
    auto threads = kHashRingUtil->get_responsible_threads_metadata(
            key, global_memory_hash_ring, local_memory_hash_ring);

    if (threads.size() != 0) { // In case no servers have joined yet.
        Address target_address = std::next(begin(threads), rand() % threads.size())
                ->key_request_connect_address();
        if (addr_request_map.find(target_address) == addr_request_map.end()) {
            addr_request_map[target_address].set_type(type);
            addr_request_map[target_address].set_response_address(response_address);
            // NB: response_address might not be necessary here
            // (or in other places where req_id is constructed either).
            string req_id = response_address + ":" + std::to_string(rid);
            addr_request_map[target_address].set_request_id(req_id);
            rid += 1;
        }

        return target_address;
    }

    return string();
}

void prepare_metadata_get_request(const Key &key,
                                  GlobalHashRing &global_memory_hash_ring,
                                  LocalHashRing &local_memory_hash_ring,
                                  map<Address, KeyRequest> &addr_request_map,
                                  Address response_address, unsigned &rid) {
    Address target_address = prepare_metadata_request(
            key, global_memory_hash_ring, local_memory_hash_ring, addr_request_map,
            response_address, rid, RequestType::GET);

    if (!target_address.empty()) {
        prepare_get_tuple(addr_request_map[target_address], key, LatticeType::LWW);
    }
}

void prepare_metadata_put_request(const Key &key, const string &value,
                                  GlobalHashRing &global_memory_hash_ring,
                                  LocalHashRing &local_memory_hash_ring,
                                  map<Address, KeyRequest> &addr_request_map,
                                  Address response_address, unsigned &rid) {
    Address target_address = prepare_metadata_request(
            key, global_memory_hash_ring, local_memory_hash_ring, addr_request_map,
            response_address, rid, RequestType::PUT);

    if (!target_address.empty()) {
        auto ts = generate_timestamp(0);
        prepare_put_tuple(addr_request_map[target_address], key, LatticeType::LWW,
                          serialize(ts, value));
    }
}

ServerThreadList HashRingUtilInterface::get_responsible_threads_metadata(
        const Key &key, GlobalHashRing &global_memory_hash_ring,
        LocalHashRing &local_memory_hash_ring) {
    ServerThreadList threads = responsible_global(key, kMetadataReplicationFactor,
                                                  global_memory_hash_ring);

    ServerThreadList result;
    for (const ServerThread &thread : threads) {
        Address public_ip = thread.public_ip();
        Address private_ip = thread.private_ip();
        set<unsigned> tids = responsible_local(key, kDefaultLocalReplication,
                                               local_memory_hash_ring);

        for (const unsigned &tid : tids) {
            result.push_back(ServerThread(public_ip, private_ip, tid));
        }
    }

    return result;
}

void HashRingUtilInterface::issue_replication_factor_request(
        const Address &response_address, const Key &key,
        GlobalHashRing &global_memory_hash_ring,
        LocalHashRing &local_memory_hash_ring, SocketCache &pushers,
        unsigned &seed) {

    //FIXME: The logs are only for debugging purposes
    /*
    string log_file = "asd_"+key+".txt";
    string log_name = "hash_ring_"+key;
    auto log = spdlog::basic_logger_mt(log_name, log_file, true);
    log->flush_on(spdlog::level::info);
     */

    //std::cout << "It's just a test\n";

    //log->info("DEBUG: Issue a replication factor request");
    //std::cout << "DEBUG: Issue a replication factor request\n";
    //log->info("DEBUG: \tCreates replication_key to the requested key");
    //std::cout << "DEBUG: Issue a replication factor request\n";
    //log->info("DEBUG: \tKey: '{}'",key);
    //std::cout << "DEBUG: \tKey: " + key + "\n";
    // Creates replication_key to the requested key
    Key replication_key = get_metadata_key(key, MetadataType::replication);
    //log->info("DEBUG: \tReplication Key: '{}'", replication_key);
    //
    // std::cout << "DEBUG: \tReplication Key: " + replication_key + "\n";
    auto threads = kHashRingUtil->get_responsible_threads_metadata(
            replication_key, global_memory_hash_ring, local_memory_hash_ring);



    // Find the target address for key
    Address target_address =
            std::next(begin(threads), rand_r(&seed) % threads.size())
                    ->key_request_connect_address();
    //log->info("DEBUG: Find the target address for key '{}'", key);
    //std::cout << "DEBUG: Find the target address for key " + key + "\n";
    //log->info("DEBUG: \t Target address: '{}'", target_address);
    //std::cout << "DEBUG: \t Target address: " + target_address + "\n";


    // Create a new key request for the key
    KeyRequest key_request;
    key_request.set_type(RequestType::GET);
    key_request.set_response_address(response_address);

    // Send out the request
    //log->info("DEBUG: Send out the request");
    //std::cout << "DEBUG: Send out the request\n";

    //std::cout << "DEBUG: ---> ROUTE hash_ring:\\t SEND KeyRequest ID: " + key_request.request_id() +"\n";

    prepare_get_tuple(key_request, replication_key, LatticeType::LWW);
    string serialized;
    key_request.SerializeToString(&serialized);
    kZmqUtil->send_string(serialized, &pushers[target_address]);
}
