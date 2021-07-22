//
// Created by szalay on 2020. 04. 08..
//

#include <iostream>
#include "monitor/monitoring_handlers.hpp"
#include "requests.hpp"
#include <list>

// the current request id
unsigned rid_ = 0;

void print_current_mapping(map<std::string, NodeAsPlacementDest> mapping, logger log) {
    log->info("\tCurrent Mapping ---------------------------");

    auto it = mapping.begin();

    // Iterate over the map using Iterator till end.
    while (it != mapping.end()) {
        log->info("\t{}: {}", it->second.st.public_ip(), it->second.capacity - it->second.load);
        for (const auto &replica :it->second.replicas) {
            log->info("\t\t - {}", replica);
        }
        it++;
    }
    log->info("\t-----------------------------");
}

set<Key> masters;


Address getOptimalHosts(set<Address> &hosts, map<Address, double> &access_hosts, Key state, double data_size,
                        set<Address> banned_hosts,
                        map<std::string, NodeAsPlacementDest> &mapping, map<Key, map<Key, double>> delay_matrix,
                        logger log,
                        bool capacity_check = false,
                        std::string access_type = "both") {

    log->info("---Start get Optimal Hosts for key {}", state);

    Address min_host = "there_is_no_suitable_host";
    double min_cost = std::numeric_limits<double>::infinity();
    for (const auto &candidate_host : hosts) {
        log->info("-> Candidate host: {}", candidate_host);
        // if candidate host not among the the banned hosts
        if (banned_hosts.find(candidate_host) == banned_hosts.end()) {
            if (capacity_check) {
                if (mapping[candidate_host].capacity - mapping[candidate_host].load < data_size) {
                    continue;
                }
            }
            double cost = 0;
            for (const auto &access_host:access_hosts) {
                auto rate = access_host.second;
                log->info("\t* Access Rate from host {} is: {}", access_host.first, rate);
                auto access_cost = delay_matrix[candidate_host][access_host.first] * rate;
                cost += access_cost;
                log->info("\t* Access Cost from host {} is: {}", access_host.first, access_cost);
                log->info("\t\t {} ({} - {} delay) * {} (rate) = {} (access_cost)",
                          delay_matrix[candidate_host][access_host.first], candidate_host, access_host.first, rate, access_cost);
            }
            log->info("* Sum cost: {}", cost);
            if (cost <= min_cost) {
                min_cost = cost;
                min_host = candidate_host;
            }

        }
    }
    log->info("---End get Optimal Hosts");
    return min_host;

}

std::vector<std::pair<Address, double>>
get_closest_hosts(set<Address> &hosts, map<Address, double> &access_hosts, Key state, double data_size,
                  map<std::string, NodeAsPlacementDest> &mapping, map<Key, map<Key, double>> delay_matrix) {

    // host and cost
    typedef std::pair<Address, double> pair;
    std::vector<pair> closest_hosts;

    Address min_host = "there_is_no_suitable_host";


    for (const auto &candidate_host : hosts) {

        double cost = 0;
        for (const auto &access_host:access_hosts) {
            auto rate = access_host.second;
            cost += delay_matrix[candidate_host][access_host.first] * rate;
        }
        closest_hosts.emplace_back(candidate_host, cost);
    }

    // sort the closest host vector to increasing order
    std::sort(closest_hosts.begin(), closest_hosts.end(),
              [](const pair &l, const pair &r) {
                  if (l.second != r.second)
                      return l.second < r.second;

                  return l.first < r.first;
              });

    return closest_hosts;

}


Address get_most_loaded_host(map<Address, NodeAsPlacementDest> mappings) {
    /*
    This method returns the host with the minimum free capacity.
    :return: host id
    */

    double free_space = std::numeric_limits<double>::infinity();
    Address most_loaded_node = "None";
    for (const auto host : mappings) {
        if (free_space > (host.second.capacity - host.second.load)) {
            free_space = host.second.capacity - host.second.load;
            most_loaded_node = host.first;
        }
    }
    return most_loaded_node;
}

Key get_state_to_move(Address most_loaded_node, set<Key> banned_states_for_state_to_move,
                      map<Address, NodeAsPlacementDest> mappings, map<Key, map<Address, double>> writer_rates,
                      map<Key, map<Address, double>> reader_rates, logger log) {
    /*
    This method return the state that the flooding algorithm migrate to a new host. This migration is necessary when
    a host is overloaded, i.e., its free capacity is less than zero.
    :param host: the overloaded host id
    :param banned_states: list of states what we cannot choose to migrate
    :return: state to migrate
    */

    if ((mappings[most_loaded_node].capacity - mappings[most_loaded_node].load) >= 0) {
        return "None";
    }

    // map of master key and its migration cost
    map<Key, double> master_costs;
    map<Key, double> slave_costs;

    for (const auto replica : mappings[most_loaded_node].replicas) {
        double moving_cost = 0;

        // if it is a master
        if (replica.find("slave") == std::string::npos) {

            //FIXME: It think this need to be reconsidered
            for (const auto writing_rate_obj : writer_rates[replica]) {
                moving_cost += writing_rate_obj.second;
            }
            for (const auto reading_rate_obj : reader_rates[replica]) {
                moving_cost += reading_rate_obj.second;
            }
            master_costs[replica] = moving_cost;
        }

            // if it is a slave
        else if (replica.find("slave") != std::string::npos) {

            std::string delimiter = "|";
            Key master = replica.substr(2, replica.find(delimiter));
            //FIXME: It think this need to be reconsidered
            for (const auto writing_rate_obj : writer_rates[master]) {
                moving_cost += writing_rate_obj.second;
            }
            for (const auto reading_rate_obj : reader_rates[master]) {
                moving_cost += reading_rate_obj.second;
            }
            slave_costs[replica] = moving_cost;
        }
    }

    // sort slaves according to their moving cost
    typedef std::pair<Key, double> pair;
    std::vector<pair> sorted_slave_costs;

    // copy key-value pairs from the map to the vector
    std::copy(slave_costs.begin(), slave_costs.end(),
              std::back_inserter<std::vector<pair>>(sorted_slave_costs));

    std::sort(sorted_slave_costs.begin(), sorted_slave_costs.end(),
              [](const pair &l, const pair &r) {
                  if (l.second != r.second)
                      return l.second < r.second;

                  return l.first < r.first;
              });

    if (sorted_slave_costs.size() != 0) {
        return sorted_slave_costs.begin()->first;
    }

    // sort masters according to their moving cost
    typedef std::pair<Key, double> pair;
    std::vector<pair> sorted_master_costs;

    // copy key-value pairs from the map to the vector
    std::copy(master_costs.begin(), master_costs.end(),
              std::back_inserter<std::vector<pair>>(sorted_master_costs));

    std::sort(sorted_master_costs.begin(), sorted_master_costs.end(),
              [](const pair &l, const pair &r) {
                  if (l.second != r.second)
                      return l.second < r.second;

                  return l.first < r.first;
              });


    if (sorted_master_costs.size() != 0) {
        return sorted_master_costs.begin()->first;
    }

    //FIXME:
    log->error("Method 'get_state_to_move' has failed.");
    return "ERROR";

}

bool is_aa_node(Key state, NodeAsPlacementDest host) {
    /*
    Check whether the given host is appropriate to the state to store it, i.e., it does not contain slaves of the given state.
    :param state: state to be moved
    :param host: host where the state could be moved to
    :return: True or False
    */

    // if it is a master
    if (state.find("slave") == std::string::npos) {
        bool contains_slave = false;
        for (const auto &replica : host.replicas) {
            if ((replica.find("slave|") != std::string::npos) && (replica.find("|" + state) != std::string::npos)) {
                contains_slave = true;
                break;
            }
        }
        return contains_slave;
    }

        // if it is a slave
    else if (state.find("slave") != std::string::npos) {
        bool contains_slave_or_master = false;
        std::string delimiter = "|";
        Key master = state.substr(2, state.find(delimiter));
        // if host contains the master
        if (host.replicas.find(master) != host.replicas.end()) {
            return true;
        }
        for (const auto &replica : host.replicas) {
            if ((replica.find("slave|") != std::string::npos) && (replica.find("|" + master) != std::string::npos)) {
                contains_slave_or_master = true;
                break;
            }
        }
        return contains_slave_or_master;
    }

    return false;
}

bool move_is_okay(const NodeAsPlacementDest &src_host, const NodeAsPlacementDest &dst_host, const Key &state,
                  double state_size,
                  map<std::string, NodeAsPlacementDest> mapping) {
    /*
    Returns whether migrating a state is possible or not
    :param src_host: the host from where the state could be moved
    :param dst_host: the host where the state could move to
    :param state: state to be moved
    :return: True or False
    */

    if ((src_host.capacity - src_host.load) < 0) {
        if ((dst_host.capacity - dst_host.load) - state_size >= 0) {
            if (!is_aa_node(state, dst_host)) {
                return true;
            }
        }
    } else {
        if ((src_host.capacity - src_host.load) < (dst_host.capacity - dst_host.load) - state_size) {
            if (!is_aa_node(state, dst_host)) {
                return true;
            }
        }
    }

    return false;
}


void flooding_placement_handler(logger log, string &serialized, GlobalRingMap &global_hash_rings, SocketCache &pushers,
                                map<Key, KeyReplication> &key_replication_map, set<Key> &local_change_set,
                                map<Key, map<Key, double>> &delay_matrix,
                                map<std::string, NodeAsPlacementDest> &mapping,
                                map<Key, set<std::string>> &slaves_of_master,
                                map<Key, map<Address, double>> &writer_rates,
                                map<Key, map<Address, double>> &reader_rates, zmq::socket_t &response_puller,
                                bool replacement) {

    KeyRequest request;
    request.ParseFromString(serialized);


    KeyRequest forwarded_request;
    forwarded_request.set_type(request.type());
    forwarded_request.set_response_address(request.response_address());
    forwarded_request.set_request_id(request.request_id());

    log->info("### FLOODING Placement module #################################################");

    log->info("Iterating through the keys...");
    // iterating through the keys
    for (const auto &tuple : request.tuples()) {

        Key key = tuple.key();
        string payload = tuple.payload();

        log->info("\tChoosen key: {}", key);

        KeyTuple *tp = forwarded_request.add_tuples();
        tp->set_key(key);
        tp->set_lattice_type(tuple.lattice_type());
        tp->set_payload(payload);


        Address master_target_address;
        Address master_address;
        set<Address> slave_target_addresses;
        set<Address> slave_addresses;

        bool slave_chosen = false;

        Address old_master_host;
        set<Address> old_slave_hosts;

        // If the key has not been stored yet in the cluster
        if (key_replication_map.find(key) == key_replication_map.end() || replacement) {

            //log->info("\tThe requested key '{}' has not stored yet in the cluster", key);

            //FIXME: masters-be az összes key-t beletenni
            masters.insert(key);
            // key = master

            ///////////////////////////////////////

            // Creating SAD (State Access Descriptor)
            for (const auto reading : tuple.reader_rates()) {
                reader_rates[key][reading.first] = reading.second;
            }
            for (const auto writing : tuple.writer_rates()) {
                writer_rates[key][writing.first] = writing.second;
            }

            int requested_slave_num = tuple.replica_num() - 1;
            requested_slave_num = 0;

            for (int i = 0; i < requested_slave_num; i++) {
                auto slave_name = "slave|" + std::to_string(i) + "|" + key;
                slaves_of_master[key].insert(slave_name);
                reader_rates[slave_name] = reader_rates[key];
            }

            log->info("DEBUG: (DELETE LATER) Reading rates:");
            for (const auto reader_rate : reader_rates) {
                log->info("\tKey: {}", reader_rate.first);
                for (const auto host_read_rate : reader_rates[reader_rate.first]) {
                    log->info("\tHost: {}, Reading rate: {}", host_read_rate.first, host_read_rate.second);
                }
            }

            log->info("DEBUG: (DELETE LATER) Writing rates:");
            for (const auto writer_rate : writer_rates) {
                log->info("\tKey: {}", writer_rate.first);
                for (const auto host_write_rate : writer_rates[writer_rate.first]) {
                    log->info("\tHost: {}, Writing rate: {}", host_write_rate.first, host_write_rate.second);
                }
            }

            // TODO: Creating Master -> Slave writing rates
            /*
            writer_rates[key]["172.17.0.2"] = 0.1;
            reader_rates[key]["192.168.0.31"] = 0.7;
            reader_rates[key]["172.17.0.2"] = 0.7;

            reader_rates["slave|0|a"]["192.168.0.31"] = 0.7;
            reader_rates["slave|0|a"]["172.17.0.2"] = 0.7;
            */
            //reader_rates["slave|0|b"]["192.168.0.31"] = 0.7;
            //reader_rates["slave|0|b"]["172.17.0.2"] = 0.7;


            ///////////////////////////////////////

            if (replacement) {
                old_master_host = key_replication_map[key].master_address_;
                old_slave_hosts = key_replication_map[key].slave_addresses_;
                //FIXME:
                old_slave_hosts.erase(old_master_host);

                mapping[old_master_host].replicas.erase(key);
                // TODO: increase hosts' load
                for (const auto slave_address : old_slave_hosts) {
                    Key slave_name;
                    for (const auto r : mapping[slave_address].replicas) {
                        if (r.find("|" + key) != std::string::npos) {
                            slave_name = r;
                            break;
                        }
                    }
                    mapping[slave_address].replicas.erase(slave_name);
                    // TODO: increase hosts' load
                }
            }

            set<Address> hosts;
            for (const ServerThread &st : global_hash_rings[Tier::MEMORY].get_unique_servers()) {
                hosts.insert(st.public_ip());
            }

            log->info("\tAvailable hosts in the cluster:");
            for (const auto &h :hosts) {
                log->info("\t\t - {}", h);
            }

            //#### Orchestration ###############################################################################################

            log->info("\tStart Orchestration...\n");
            print_current_mapping(mapping, log);

            // TODO: Ordering states - Line 3 from pseudocode
            auto sorted_keys = masters;

            // Creating temporary mappings without checking hosts' capacity
            log->info("\t\t1. Creating temporary mappings without checking hosts' capacity");
            for (const auto &s : masters) {

                // set of hosts accessing the state                                                      || pseudocode:7
                map<Address, double> access_hosts;
                double sum_writer_rate = 0.0;

                auto it = reader_rates[s].begin();
                while (it != reader_rates[s].end()) {
                    access_hosts[it->first] = it->second;
                    it++;
                }

                auto wit = writer_rates[s].begin();
                while (wit != writer_rates[s].end()) {
                    access_hosts[wit->first] += wit->second;
                    sum_writer_rate += wit->second;
                    wit++;
                }

                // if optimal placement solution can be found in polinomial time ///////////////////////////////////////

                set<Address> banned_hosts;

                // if there is no replica                                                                || pseudocode:8
                if (slaves_of_master[s].empty()) {
                    log->info("\t\t\t1.1 If there is no replica, calculating optimal host");
                    Address candidate_host = getOptimalHosts(hosts, access_hosts, s, sizeof(payload), banned_hosts,
                                                             mapping, delay_matrix, log);

                    log->info("\t\t\t1.2 Candidate host: {}", candidate_host);
                    mapping[candidate_host].load += sizeof(payload);
                    mapping[candidate_host].replicas.insert(s);
                }

                    // If the state does have slave replica(s), and is read by only one host            || pseudocode:11
                else if (reader_rates[s].size() == 1) {
                    log->info("\t\t\tIf the state does have slave replica(s), and is read by only one host");
                    //pseudocode:12
                    Address candidate_host_for_master = getOptimalHosts(hosts, access_hosts, s, sizeof(payload),
                                                                        banned_hosts, mapping,
                                                                        delay_matrix, log);

                    mapping[candidate_host_for_master].load += sizeof(payload);
                    mapping[candidate_host_for_master].replicas.insert(s);
                    banned_hosts.insert(candidate_host_for_master);

                    // Iterating through the master's slaves
                    map<Address, double> master_access;
                    master_access[candidate_host_for_master] = sum_writer_rate;
                    for (const auto &slave : slaves_of_master[s]) {                                  // || pseudocode:14
                        Address candidate_host_for_slave = getOptimalHosts(hosts, master_access, slave, sizeof(payload),
                                                                           banned_hosts,
                                                                           mapping, delay_matrix, log);

                        mapping[candidate_host_for_slave].load += sizeof(payload);
                        mapping[candidate_host_for_slave].replicas.insert(slave);
                        banned_hosts.insert(candidate_host_for_slave);
                    }
                }


                    // If the state does have slave replica(s), and is read by multiple function instance
                else {
                    log->info("\t\t\tstate have slave replica(s), and is read by multiple function instance");

                    // Sorting reader hosts according their reading rates in decreasing order --------------------------
                    //FIXME: we could use originally vector instead of master
                    typedef std::pair<Address, double> pair;
                    std::vector<pair> sorted_reader_hosts;

                    // copy key-value pairs from the map to the vector
                    std::copy(reader_rates[s].begin(), reader_rates[s].end(),
                              std::back_inserter<std::vector<pair>>(sorted_reader_hosts));

                    std::sort(sorted_reader_hosts.begin(), sorted_reader_hosts.end(),
                              [](const pair &l, const pair &r) {
                                  if (l.second != r.second)
                                      return l.second > r.second;

                                  return l.first < r.first;
                              });

                    // list of replica <-> reader hosts assignments ------------------------------------------------
                    //typedef map<std::string, Address> Assignment;
                    typedef std::pair<std::string, Address> Assignment;
                    typedef std::list<Assignment> AssignmentList;

                    AssignmentList assignments;

                    // If more reader hosts exist than the required slave replica number -------------------------------

                    // Spread out replicas over the functions
                    if (requested_slave_num <
                        reader_rates[s].size()) {                            // || pseudocode: 23
                        log->info("\t\t\tMore reader hosts exist than the required slave replica number");
                        int i = 0;
                        for (const auto slave : slaves_of_master[s]) {
                            if (i < requested_slave_num) {
                                Assignment a;
                                a.first = slave;
                                a.second = sorted_reader_hosts[i].first;
                                assignments.push_back(a);
                                i++;
                            } else {
                                break;
                            }

                        }

                        for (i = requested_slave_num + 1; i < reader_rates[s].size(); i++) {
                            Assignment a;
                            a.first = s;
                            a.second = sorted_reader_hosts[i].first;
                            assignments.push_back(a);
                        }
                    }

                        // If the replica number is greater then the number of RO functions --------------------------------

                        // Spread out replicas over the functions                                           || pseudocode:28
                    else if (requested_slave_num >= reader_rates[s].size()) {
                        log->info("\t\t\tReplica number is greater equal then the number of reader hosts");
                        int i = 0;
                        for (const auto slave : slaves_of_master[s]) {
                            if (i < reader_rates[s].size()) {
                                Assignment a;
                                a.first = slave;
                                a.second = sorted_reader_hosts[i].first;
                                assignments.push_front(a);
                                i++;
                            } else {
                                break;
                            }
                        }
                    }

                    // Assign other (writer) functions -----------------------------------------------------------------
                    for (const auto writer_host: writer_rates[s]) {                                  // || pseudocode:32
                        Assignment a;
                        a.first = s;
                        a.second = writer_host.first;
                        assignments.push_back(a);
                    }

                    //TODO: Assign master state to slaves

                    // First, place the master state
                    map<Address, double> assigned_hosts;
                    for (const auto assigned_pair : assignments) {
                        if (assigned_pair.first == s) {
                            assigned_hosts[assigned_pair.second] = writer_rates[s][assigned_pair.second];
                        }
                    }

                    // Find the optimal destination hosts for master without hosts' capacity check
                    Address candidate_host = getOptimalHosts(hosts, assigned_hosts, s, sizeof(payload), banned_hosts,
                                                             mapping,
                                                             delay_matrix, log);

                    // Deploy state to the found host
                    mapping[candidate_host].load += sizeof(payload);
                    mapping[candidate_host].replicas.insert(s);
                    banned_hosts.insert(candidate_host);

                    print_current_mapping(mapping, log);

                    // And in this for loop, we place all the slaves independently the free capacities of the hosts
                    for (const auto slave : slaves_of_master[s]) {
                        assigned_hosts.clear();
                        for (const auto assigned_pair : assignments) {
                            if (assigned_pair.first == slave) {
                                assigned_hosts[assigned_pair.second] = reader_rates[slave].find(
                                        assigned_pair.second)->second;
                            }
                        }

                        // Find the optimal destination hosts for master without hosts' capacity check
                        Address candidate_host_for_slave = getOptimalHosts(hosts, assigned_hosts, slave,
                                                                           sizeof(payload), banned_hosts,
                                                                           mapping,
                                                                           delay_matrix, log);
                        if (candidate_host_for_slave == "there_is_no_suitable_host") {
                            log->error("Placement: There is no suitable host for key {}", slave);
                            //FIXME: Throw an exception
                            break;
                        }
                        // Deploy state to the found host
                        mapping[candidate_host_for_slave].load += sizeof(payload);
                        mapping[candidate_host_for_slave].replicas.insert(slave);
                        banned_hosts.insert(candidate_host_for_slave);

                        print_current_mapping(mapping, log);
                    }


                }


                print_current_mapping(mapping, log);


                for (const auto map:mapping) {
                    for (const auto replica: map.second.replicas) {
                        if (replica == s) {
                            master_target_address = map.second.st.store_request_connect_address();
                            master_address = map.second.st.public_ip();
                            break;
                        } else if (replica.find("|" + s) != std::string::npos) {
                            slave_target_addresses.insert(map.second.st.store_request_connect_address());
                            slave_addresses.insert(map.second.st.public_ip());
                        }
                    }
                }
            }

            // And now it's time for capacity checking :) --------------------------------------------------------------

            log->info("\t\t2. It's time for capacity checking");
            //Save states into this set which are not allowed to move from one host to another
            set<Key> banned_states_for_state_to_move;

            // Pick up the most loaded host
            Address most_loaded_host = get_most_loaded_host(mapping);

            // Pick the state what we want to move
            Key state_to_move = get_state_to_move(most_loaded_host, banned_states_for_state_to_move, mapping,
                                                  writer_rates, reader_rates, log);

            if (state_to_move != "None") {
                log->info("\t\t\tState movement is necessary...\n");
            }

            int break_iter = 0;
            while (state_to_move != "None" && break_iter < 5) {

                //FIXME:
                if (state_to_move == "ERROR") {
                    //TODO: Throw an exception
                    log->error("State placement failed :(");
                    print_current_mapping(mapping, log);
                }

                // get access hosts
                map<Address, double> access_hosts;
                // if state_to_move is a slave
                if (state_to_move.find("slave") != std::string::npos) {
                    std::string delimiter = "|";
                    Key master_of_state_to_move = state_to_move.substr(2, state_to_move.find(delimiter));

                    for (const auto iter : reader_rates[master_of_state_to_move]) {
                        access_hosts[iter.first] = iter.second;
                    }
                    for (const auto iter : writer_rates[master_of_state_to_move]) {
                        access_hosts[iter.first] += iter.second;
                    }
                }
                    // if state_to_move is a master
                else {
                    for (const auto iter : reader_rates[state_to_move]) {
                        access_hosts[iter.first] = iter.second;
                    }
                    for (const auto iter : writer_rates[state_to_move]) {
                        access_hosts[iter.first] += iter.second;
                    }
                }

                print_current_mapping(mapping, log);

                set<Address> banned_hosts;
                std::vector<std::pair<Address, double>> closest_nodes = get_closest_hosts(hosts, access_hosts,
                                                                                          state_to_move,
                                                                                          sizeof(payload), mapping,
                                                                                          delay_matrix);
                bool state_moving_failed = true;

                for (const auto candidate_h : closest_nodes) {
                    if (most_loaded_host != candidate_h.first) {
                        if (move_is_okay(mapping[most_loaded_host], mapping[candidate_h.first], state_to_move,
                                         sizeof(payload), mapping)) {
                            // if state_to_move is a master
                            if (state_to_move.find("slave") == std::string::npos) {
                                mapping[most_loaded_host].replicas.erase(state_to_move);
                                mapping[most_loaded_host].load -= sizeof(payload);
                                mapping[candidate_h.first].load += sizeof(payload);
                                mapping[candidate_h.first].replicas.insert(state_to_move);
                                state_moving_failed = false;
                                break;
                            }

                                // if state_to_move is a slave
                            else if (!is_aa_node(state_to_move, mapping[candidate_h.first])) {
                                mapping[most_loaded_host].replicas.erase(state_to_move);
                                mapping[most_loaded_host].load -= sizeof(payload);
                                mapping[candidate_h.first].load += sizeof(payload);
                                mapping[candidate_h.first].replicas.insert(state_to_move);
                                state_moving_failed = false;
                                break;
                            }
                        }
                    }
                }
                if (state_moving_failed) {
                    banned_states_for_state_to_move.insert(state_to_move);
                }

                // Pick up the most loaded host
                most_loaded_host = get_most_loaded_host(mapping);
                // Pick the state what we want to move
                state_to_move = get_state_to_move(most_loaded_host, banned_states_for_state_to_move, mapping,
                                                  writer_rates, reader_rates, log);

                break_iter++;
            }

            // Clean the unnecessary settings
            //FIXME: maybe another reader_rates map should be used in the flooding placement method
            // and not the same which is declared in monitoring.cpp
            for (const auto &m : masters) {
                for (const auto &slave: slaves_of_master[m]) {
                    reader_rates.erase(slave);
                }
            }

            masters.clear();

            log->info("### FLOODING placement has finished ###########################################\n");

	    tp->set_master(master_address);

        }

            // If the key has been already stored in the cluster
        else {
            for (const ServerThread &st : global_hash_rings[Tier::MEMORY].get_unique_servers()) {
                if (key_replication_map[key].master_address_ == st.public_ip()) {

                    if (request.type() == RequestType::GET){
			master_target_address = st.key_request_connect_address();
                        //log->info("BOOTSTRAP:  The requested key '{}' has already been stored in the cluster, since it is a GET operation, the request is forwarded to server '{}'", key, master_target_address);
                    }
                    else {
                        master_target_address = st.store_request_connect_address();
                        //log->info("BOOTSTRAP:  The requested key '{}' has already been stored in the cluster, since it is a PUT operation, the request is forwarded to server '{}'", key, master_target_address);
                    }

                    //master_target_address = st.store_request_connect_address();
                    break;
                }
            }
            tp->set_master(key_replication_map[key].master_address_);

        }

        // Sending <KeyRequest> messages to the KVSes //////////////////////////////////////////////////////////////////

        if (replacement) {

            log->info("DEBUG: REPLACEMENT HAS FINISHED...");

            // If nothing changed
            if (old_master_host == master_address && old_slave_hosts == slave_addresses) {
                log->info("if (old_master_host == master_address && old_slave_hosts == slave_addresses)");
                log->info("if ({} == {} && {} == {})", old_master_host, master_address);
                log->info("DEBUG: AFTER REPLACEMENT nothing has changed");
            }
            // If the master state has changed
            if (old_master_host != master_address) {
                log->info("DEBUG: AFTER REPLACEMENT master state has changed");

                //Send out request to the new master host
                string serialized_req;
                forwarded_request.SerializeToString(&serialized_req);
                log->info("DEBUG: Send <KeyRequest> to the new master {}", master_target_address);
                kZmqUtil->send_string(serialized_req, &pushers[master_target_address]);

                bool succeed;
                vector<KeyResponse> responses;
                set<string> req_ids{forwarded_request.request_id()};
                //succeed = receive<KeyResponse>(response_puller, req_ids, responses);
                log->info("DEBUG: And waiting for the answer...");
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


                if (succeed) {
                    log->info("DEBUG: New master has saved.");
                    // Delete old master
                    log->info("DEBUG: Forward DELETE <KeyRequest> to the OLD master KVS {}", old_master_host);
                    Address old_master_target_address;
                    for (const ServerThread &st : global_hash_rings[Tier::MEMORY].get_unique_servers()) {
                        if (old_master_host == st.public_ip()) {
                            old_master_target_address = st.store_request_connect_address();
                            break;
                        }
                    }
                    forwarded_request.set_type(RequestType::DEL);
                    string serialized_req2;
                    forwarded_request.SerializeToString(&serialized_req2);
                    kZmqUtil->send_string(serialized_req2, &pushers[old_master_target_address]);
                } else {
                    log->error("Saving new master failed.");
                    // TODO: In this case the key_replication_maps should show the old state of the mapping
                }

            }
            // If the slaves have changed
            if (old_slave_hosts != slave_addresses) {
                log->info("DEBUG: AFTER REPLACEMENT slaves have changed");

                // Send out place request to the new slaves
                for (const auto sa : slave_addresses) {
                    if (old_slave_hosts.find(sa) == old_slave_hosts.end()) {
                        log->info("DEBUG: Forward <KeyRequest> to the NEW Slave KVS {}", sa);
                        string serialized_req2;
                        forwarded_request.SerializeToString(&serialized_req2);
                        kZmqUtil->send_string(serialized_req2, &pushers[sa]);
                    }
                }

                // Delete the data from where it will not be anymore
                for (const auto sa : old_slave_hosts) {
                    if (slave_addresses.find(sa) == slave_addresses.end()) {
                        log->info("DEBUG: Forward DELETE <KeyRequest> to the OLD Slave KVS {}", sa);
                        forwarded_request.set_type(RequestType::DEL);
                        string serialized_req2;
                        forwarded_request.SerializeToString(&serialized_req2);
                        kZmqUtil->send_string(serialized_req2, &pushers[sa]);
                    }
                }
            }

            for (const auto host : old_slave_hosts) {
                if (slave_addresses.find(host) != slave_addresses.end()) {
                    mapping[host].load -= sizeof(payload);
                }
            }
            if (old_master_host == master_address) {
                mapping[old_master_host].load -= sizeof(payload);
            }

        } else {
            log->info("BOOTSTRAP: Forward MASTER <KeyRequest> (formerly <StoreReq>) to the KVS {}",
                      master_target_address);
            string serialized_req;
            forwarded_request.SerializeToString(&serialized_req);
            kZmqUtil->send_string(serialized_req, &pushers[master_target_address]);

            //Send the request to the slaves as well.
            for (const auto sa : slave_target_addresses) {
                log->info("BOOTSTRAP: Forward SLAVE <KeyRequest> (formerly <StoreReq>) to the KVS {}", sa);
                string serialized_req2;
                forwarded_request.SerializeToString(&serialized_req2);
                kZmqUtil->send_string(serialized_req2, &pushers[sa]);
            }
        }

        // If the key has not been stored yet in the cluster
        if (key_replication_map.find(key) == key_replication_map.end() || replacement) {

            key_replication_map.erase(key);

            // Store key in the key_replication_map
            init_replication(key_replication_map, key);
            key_replication_map[key].master_address_ = master_address;
            key_replication_map[key].slave_addresses_.insert(master_address);
            for (const auto sa : slave_addresses) {
                key_replication_map[key].slave_addresses_.insert(sa);
            }

            local_change_set.insert(key);
        }

    }
}
