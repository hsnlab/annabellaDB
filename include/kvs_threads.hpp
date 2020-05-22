
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

#ifndef KVS_INCLUDE_THREADS_HPP_
#define KVS_INCLUDE_THREADS_HPP_

#include "threads.hpp"
#include "types.hpp"

// The port on which KVS servers listen for new node announcments.
const unsigned kNodeJoinPort = 6000;

// The port on which KVS servers listen for node departures.
const unsigned kNodeDepartPort = 6050;

// The port on which KVS servers are asked to depart by the monitoring system.
const unsigned kSelfDepartPort = 6100;

// The port on which KVS servers listen for replication factor responses.
const unsigned kServerReplicationResponsePort = 6150;

// The port on which KVS servers listen for requests for data.
const unsigned kKeyRequestPort = 6200;

const unsigned kKeyRequestPort_Monitor = 5900;


const unsigned dDelayRequestPort = 6270;
const unsigned dDelayResponsePort = 6280;

// The port on which KVS servers listen for gossip from other KVS nodes.
const unsigned kGossipPort = 6250;

// The port on which KVS servers listen for a replication factor change from
// the monitoring system.
const unsigned kServerReplicationChangePort = 6300;

// The port on which KVS servers listen for responses to a request for the list
// of all existing caches.
const unsigned kCacheIpResponsePort = 7050;

// The port on which routing servers listen for cluster membership requests.
const unsigned kSeedPort = 6350;

// The port on which routing servers listen for cluster membership changes.
const unsigned kRoutingNotifyPort = 6400;

// The port on which routing servers listen for replication factor responses.
const unsigned kRoutingReplicationResponsePort = 6500;

// The port on which routing servers listen for replication factor change
// announcements from the monitoring system.
const unsigned kRoutingReplicationChangePort = 6550;

// The port on which the monitoring system listens for cluster membership
// changes.
const unsigned kMonitoringNotifyPort = 6600;

// The port on which monitoring threads listen for KVS responses when
// retrieving metadata.
const unsigned kMonitoringResponsePort = 6650;

//FIXME:
// The port on which route threads listen for KVS responses when
// retrieving the address of master replicas.
const unsigned kRoutingResponsePort = 6651;

// The port on which the monitoring system waits for a response from KVS nodes
// after they have finished departing.
const unsigned kDepartDonePort = 6700;

// The port on which the monitoring nodes listens for performance feedback from
// clients.
const unsigned kFeedbackReportPort = 6750;

// The port on which benchmark nodes listen for triggers.
const unsigned kBenchmarkCommandPort = 6900;

// The port on which storage nodes retrieve their restart counts from the
// management system.
const unsigned kKopsRestartCountPort = 7000;

// The port on which the management server will listen for requests for
// executor nodes.
const unsigned kKopsFuncNodesPort = 7002;

const unsigned kMonitoringDelayReportPort = 7100;

const unsigned kPlacementReqPort = 7350;
const unsigned kPlacementRespPort = 7200;

const unsigned kStoreReqPort = 7250;

const unsigned kUpdatePort = 7400;

const unsigned kRouteUpdatePort = 7450;

const unsigned kUpdateReqPort = 7600;

class ServerThread {
    Address public_ip_;
    Address public_base_;

    Address private_ip_;
    Address private_base_;

    unsigned tid_;
    unsigned virtual_num_;

public:
    ServerThread() {}

    ServerThread(Address public_ip, Address private_ip, unsigned tid)
            : public_ip_(public_ip), private_ip_(private_ip),
              private_base_("tcp://" + private_ip_ + ":"),
              public_base_("tcp://" + public_ip_ + ":"), tid_(tid) {}

    ServerThread(Address public_ip, Address private_ip, unsigned tid,
                 unsigned virtual_num)
            : public_ip_(public_ip), private_ip_(private_ip),
              private_base_("tcp://" + private_ip_ + ":"),
              public_base_("tcp://" + public_ip_ + ":"), tid_(tid),
              virtual_num_(virtual_num) {}

    Address public_ip() const { return public_ip_; }

    Address private_ip() const { return private_ip_; }

    unsigned tid() const { return tid_; }

    unsigned virtual_num() const { return virtual_num_; }

    string id() const { return private_ip_ + ":" + std::to_string(tid_); }

    string virtual_id() const {
        return private_ip_ + ":" + std::to_string(tid_) + "_" +
               std::to_string(virtual_num_);
    }

    Address node_join_connect_address() const {
        return private_base_ + std::to_string(tid_ + kNodeJoinPort);
    }

    Address node_join_bind_address() const {
        return kBindBase + std::to_string(tid_ + kNodeJoinPort);
    }

    Address node_depart_connect_address() const {
        return private_base_ + std::to_string(tid_ + kNodeDepartPort);
    }

    Address node_depart_bind_address() const {
        return kBindBase + std::to_string(tid_ + kNodeDepartPort);
    }

    Address self_depart_connect_address() const {
        return private_base_ + std::to_string(tid_ + kSelfDepartPort);
    }

    Address self_depart_bind_address() const {
        return kBindBase + std::to_string(tid_ + kSelfDepartPort);
    }

    Address key_request_connect_address() const {
        return public_base_ + std::to_string(tid_ + kKeyRequestPort);
    }

    Address key_request_bind_address() const {
        return kBindBase + std::to_string(tid_ + kKeyRequestPort);
    }

    Address update_connect_address() const {
        return public_base_ + std::to_string(tid_ + kUpdatePort);
    }

    Address update_bind_address() const {
        return kBindBase + std::to_string(tid_ + kUpdatePort);
    }

    Address delay_request_connect_address() const {
        return public_base_ + std::to_string(tid_ + dDelayRequestPort);
    }

    Address delay_request_bind_address() const {
        return kBindBase + std::to_string(tid_ + dDelayRequestPort);
    }

    Address delay_response_connect_address() const {
        return public_base_ + std::to_string(tid_ + dDelayResponsePort);
    }

    Address delay_response_bind_address() const {
        return kBindBase + std::to_string(tid_ + dDelayResponsePort);
    }

    Address store_request_connect_address() const {
        return public_base_ + std::to_string(tid_ + kStoreReqPort);
    }

    Address store_request_bind_address() const {
        return kBindBase + std::to_string(tid_ + kStoreReqPort);
    }

    Address slave_update_connect_address() const {
        return public_base_ + std::to_string(tid_ + kUpdateReqPort);
    }

    Address slave_update_bind_address() const {
        return kBindBase + std::to_string(tid_ + kUpdateReqPort);
    }

    Address replication_response_connect_address() const {
        return private_base_ +
               std::to_string(tid_ + kServerReplicationResponsePort);
    }

    Address replication_response_bind_address() const {
        return kBindBase + std::to_string(tid_ + kServerReplicationResponsePort);
    }

    Address cache_ip_response_connect_address() const {
        return private_base_ + std::to_string(tid_ + kCacheIpResponsePort);
    }

    Address cache_ip_response_bind_address() const {
        return kBindBase + std::to_string(tid_ + kCacheIpResponsePort);
    }

    Address gossip_connect_address() const {
        return private_base_ + std::to_string(tid_ + kGossipPort);
    }

    Address gossip_bind_address() const {
        return kBindBase + std::to_string(tid_ + kGossipPort);
    }

    Address replication_change_connect_address() const {
        return private_base_ + std::to_string(tid_ + kServerReplicationChangePort);
    }

    Address replication_change_bind_address() const {
        return kBindBase + std::to_string(tid_ + kServerReplicationChangePort);
    }
};

inline bool operator==(const ServerThread &l, const ServerThread &r) {
    if (l.id().compare(r.id()) == 0) {
        return true;
    } else {
        return false;
    }
}

class RoutingThread {
    Address ip_;
    Address ip_base_;
    unsigned tid_;

public:
    RoutingThread() {}

    RoutingThread(Address ip, unsigned tid)
            : ip_(ip), tid_(tid), ip_base_("tcp://" + ip_ + ":") {}

    Address ip() const { return ip_; }

    unsigned tid() const { return tid_; }

    Address seed_connect_address() const {
        return ip_base_ + std::to_string(tid_ + kSeedPort);
    }

    Address seed_bind_address() const {
        return kBindBase + std::to_string(tid_ + kSeedPort);
    }

    Address notify_connect_address() const {
        return ip_base_ + std::to_string(tid_ + kRoutingNotifyPort);
    }

    Address notify_bind_address() const {
        return kBindBase + std::to_string(tid_ + kRoutingNotifyPort);
    }

    Address key_address_connect_address() const {
        return ip_base_ + std::to_string(tid_ + kKeyAddressPort);
    }

    Address key_address_bind_address() const {
        return kBindBase + std::to_string(tid_ + kKeyAddressPort);
    }

    Address placement_req_connect_address() const {
        return ip_base_ + std::to_string(tid_ + kPlacementReqPort);
    }

    Address placement_req_bind_address() const {
        return kBindBase + std::to_string(tid_ + kPlacementRespPort);
    }


    Address response_connect_address() const {
      return ip_base_ + std::to_string(kRoutingResponsePort);
    }

    Address response_bind_address() const {
      return kBindBase + std::to_string(kRoutingResponsePort);
    }


    Address replication_response_connect_address() const {
        return ip_base_ + std::to_string(tid_ + kRoutingReplicationResponsePort);
    }

    Address replication_response_bind_address() const {
        return kBindBase + std::to_string(tid_ + kRoutingReplicationResponsePort);
    }

    Address replication_change_connect_address() const {
        return ip_base_ + std::to_string(tid_ + kRoutingReplicationChangePort);
    }

    Address replication_change_bind_address() const {
        return kBindBase + std::to_string(tid_ + kRoutingReplicationChangePort);
    }

    Address update_connect_address() const {
        return ip_base_ + std::to_string(tid_ + kRouteUpdatePort);
    }

    Address update_bind_address() const {
        return kBindBase + std::to_string(tid_ + kRouteUpdatePort);
    }

};

class MonitoringThread {
    Address ip_;
    Address ip_base_;

public:
    MonitoringThread() {}

    MonitoringThread(Address ip) : ip_(ip), ip_base_("tcp://" + ip_ + ":") {}

    Address ip() const { return ip_; }

    Address notify_connect_address() const {
        return ip_base_ + std::to_string(kMonitoringNotifyPort);
    }

    Address notify_bind_address() const {
        return kBindBase + std::to_string(kMonitoringNotifyPort);
    }

    Address report_delay_address() const {
        return ip_base_ + std::to_string(kMonitoringDelayReportPort);
    }

    Address delay_report_bind_address() const {
        return kBindBase + std::to_string(kMonitoringDelayReportPort);
    }

    Address placement_req_connect_address() const {
        return ip_base_ + std::to_string(kPlacementReqPort);
    }

    Address response_connect_address() const {
        return ip_base_ + std::to_string(kMonitoringResponsePort);
    }

    Address response_bind_address() const {
        return kBindBase + std::to_string(kMonitoringResponsePort);
    }

    Address depart_done_connect_address() const {
        return ip_base_ + std::to_string(kDepartDonePort);
    }

    Address depart_done_bind_address() const {
        return kBindBase + std::to_string(kDepartDonePort);
    }

    Address feedback_report_connect_address() const {
        return ip_base_ + std::to_string(kFeedbackReportPort);
    }

    Address feedback_report_bind_address() const {
        return kBindBase + std::to_string(kFeedbackReportPort);
    }

    Address key_request_connect_address() const {
        return ip_base_ + std::to_string(kKeyRequestPort_Monitor);
    }

    Address key_request_bind_address() const {
        return kBindBase + std::to_string(kKeyRequestPort_Monitor);
    }
};

class BenchmarkThread {
public:
    BenchmarkThread() {}

    BenchmarkThread(Address ip, unsigned tid) : ip_(ip), tid_(tid) {}

    Address ip() const { return ip_; }

    unsigned tid() const { return tid_; }

    Address benchmark_command_address() const {
        return "tcp://" + ip_ + ":" + std::to_string(tid_ + kBenchmarkCommandPort);
    }

private:
    Address ip_;
    unsigned tid_;
};

inline string get_join_count_req_address(string management_ip) {
    return "tcp://" + management_ip + ":" + std::to_string(kKopsRestartCountPort);
}

inline string get_func_nodes_req_address(string management_ip) {
    return "tcp://" + management_ip + ":" + std::to_string(kKopsFuncNodesPort);
}

struct ThreadHash {
    std::size_t operator()(const ServerThread &st) const {
        return std::hash<string>{}(st.id());
    }
};

#endif // KVS_INCLUDE_THREADS_HPP_
