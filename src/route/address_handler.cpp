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
#include "route/routing_handlers.hpp"

//FIXME:
#include "common.hpp"
#include "requests.hpp"
#include "threads.hpp"
#include "types.hpp"

// the current request id
unsigned rid_ = 0;


/**
     * Generates a unique request ID.
     */
string get_request_id(const RoutingThread &ut_) {

    if (++rid_ % 10000 == 0) rid_ = 0;
    return ut_.ip() + ":" + std::to_string(ut_.tid()) + "_" + std::to_string(rid_++);
}

//CHECKME:
/**
     * Prepare a data request object by populating the request ID, the key for
     * the request, and the response address. This method modifies the passed-in
     * KeyRequest and also returns a pointer to the KeyTuple contained by this
     * request.
     */
KeyTuple *prepare_data_request(KeyRequest &request, const Key &key, RoutingThread rt_) {

    request.set_request_id(get_request_id(rt_));
    request.set_response_address(rt_.response_connect_address());

    KeyTuple *tp = request.add_tuples();
    tp->set_key(key);

    return tp;
}

void address_handler(logger log, string &serialized, SocketCache &pushers,
                     RoutingThread &rt, GlobalRingMap &global_hash_rings,
                     LocalRingMap &local_hash_rings,
                     map<Key, KeyReplication> &key_replication_map,
                     map<Key, vector<pair<Address, string>>> &pending_requests,
                     unsigned &seed, Address local_address, Address monitoring_ip) {
    KeyAddressRequest addr_request;
    addr_request.ParseFromString(serialized);

    //log->info("DEBUG: ---> ROUTE address_handler:\t Receive KeyAddressRequest ID: {}, KEY: {}",
    //          addr_request.request_id(), addr_request.keys()[0]);

    KeyAddressResponse addr_response;
    addr_response.set_response_id(addr_request.request_id());
    bool succeed;

    // Number of servers in the cluster where key could be stored
    int num_servers = 0;
    for (const auto &pair : global_hash_rings) {
        num_servers += pair.second.size();
    }

    bool respond = false;
    // if no servers exist in the cluster
    if (num_servers == 0) {
        addr_response.set_error(AnnaError::NO_SERVERS);

        for (const Key &key : addr_request.keys()) {
            KeyAddressResponse_KeyAddress *tp = addr_response.add_addresses();
            tp->set_key(key);
        }

        respond = true;
    }
        //TODO: If the bootstrap server is unavailable


        // if there are servers, attempt to forward the request to the correct server where the key is located
    else {

        for (const Key &key : addr_request.keys()) {

            // std::cout << "ROUTE: GET KeyAddressRequest message for key '" + key + "'\n";

            ServerThreadList threads = {};

            for (const Tier &tier : kAllTiers) {

                // Checking whether it is already stored in the KVS or that's the first request of the key
                if (key_replication_map.find(key) == key_replication_map.end()) {

                    // gives back the bootstrap IP
                    KeyAddressResponse_KeyAddress *tp = addr_response.add_addresses();
                    tp->set_key(key);
                    respond = true;
                    tp->add_ips(MonitoringThread(monitoring_ip).key_request_connect_address());
                    //log->info(
                    //        "ROUTE: requested key '{}' is not stored in key_replication_map. Giving back the Bootstrap's IP {}.",
                    //        key,
                    //        MonitoringThread(monitoring_ip).key_request_connect_address());

                    if (addr_request.query_type() == "PUT") {
			//log->info("ROUTE: request type of key '{}' is PUT", key);
                        tp->set_request_type(RequestType::PUT);
                    }
                    else if(addr_request.query_type() == "GET"){
			//log->info("ROUTE: request type of key '{}' is GET", key);
                        tp->set_request_type(RequestType::GET);
                    }

                    // FIXME: Delete this key_rep_savings
                    // Store key in the key_replication_map
                    //init_replication(key_replication_map, key);
                    //key_replication_map[key].master_address_ = local_address;
                    break;
                }

                    // If the requested key is already stored in the KVS
                else {
                    //TODO: Gives back the right address, where the key is located
                    //log->info("ROUTE: good news, the requested key '{}' has already been stored.", key);
                    if (addr_request.query_type() == "PUT") {

                        KeyAddressResponse_KeyAddress *tp = addr_response.add_addresses();
                        tp->set_key(key);
                        respond = true;
                        //FIXME:
                        Address connect_address = "tcp://" + key_replication_map[key].master_address_ + ":" +
                                                  std::to_string(kKeyRequestPort);
                        tp->add_ips(connect_address);
                        tp->set_request_type(RequestType::PUT);
                        //log->info("ROUTE: Request is a PUT, Returning master address {}", connect_address);

                    } else if (addr_request.query_type() == "GET") {
                        //FIXME: Gives back not the master but the closest slave address
                        KeyAddressResponse_KeyAddress *tp = addr_response.add_addresses();
                        tp->set_key(key);
                        respond = true;
                        //FIXME:
                        Address slave_addr;
                        for (const auto &slave : key_replication_map[key].slave_addresses_) {
                            slave_addr = slave;
                            break;
                        }
                        Address connect_address = "tcp://" + slave_addr + ":" + std::to_string(kKeyRequestPort);
                        tp->add_ips(connect_address);
                        tp->set_request_type(RequestType::GET);
                        //log->info("ROUTE: Request is a GET, closest slave address {}", connect_address);
                    } else {
                        log->info("ROUTE: WARNING: Unknown request type: {}. Maybe you're using python client?",
                                  addr_request.query_type());
                    }

                }
            }

        }
    }

    if (respond) {

        //log->info("ROUTE: SENDING out <KeyAddressResponse> to {}",addr_request.response_address());
        string serialized;
        addr_response.SerializeToString(&serialized);

        kZmqUtil->send_string(serialized,
                              &pushers[addr_request.response_address()]);
    }
}
