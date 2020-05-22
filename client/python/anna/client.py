#  Copyright 2019 U.C. Berkeley RISE Lab
#
#  Licensed under the Apache License, Version 2.0 (the "License");
#  you may not use this file except in compliance with the License.
#  You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
#  Unless required by applicable law or agreed to in writing, software
#  distributed under the License is distributed on an "AS IS" BASIS,
#  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
#  See the License for the specific language governing permissions and
#  limitations under the License.

import random
import socket

import zmq

from anna.anna_pb2 import (
    GET, PUT,  # Anna's request types
    NO_ERROR,  # Anna's error modes
    KeyAddressRequest,
    KeyAddressResponse,
    KeyResponse
)
from anna.base_client import BaseAnnaClient
from anna.common import UserThread
from anna.zmq_util import (
    recv_response,
    send_request,
    SocketCache
)


class AnnaTcpClient(BaseAnnaClient):
    def __init__(self, elb_addr, ip, local=False, offset=0):
        '''
        The AnnaTcpClientTcpAnnaClient allows you to interact with a local
        copy of Anna or with a remote cluster running on AWS.

        elb_addr: Either 127.0.0.1 (local mode) or the address of an AWS ELB
        for the routing tier
        ip: The IP address of the machine being used -- if None is provided,
        one is inferred by using socket.gethostbyname(); WARNING: this does not
        always work
        elb_ports: The ports on which the routing tier will listen; use 6450 if
        running in local mode, otherwise do not change
        offset: A port numbering offset, which is only needed if multiple
        clients are running on the same machine
        '''

        self.elb_addr = elb_addr

        if local:
            self.elb_ports = [6450]
        else:
            self.elb_ports = list(range(6450, 6454))

        if ip:
            self.ut = UserThread(ip, offset)
        else:  # If the IP is not provided, we attempt to infer it.
            self.ut = UserThread(socket.gethostbyname(socket.gethostname()),
                                 offset)

        self.context = zmq.Context(1)

        self.get_address_cache = {}
        self.put_address_cache = {}
        self.pusher_cache = SocketCache(self.context, zmq.PUSH)

        self.response_puller = self.context.socket(zmq.PULL)
        self.response_puller.bind(self.ut.get_request_pull_bind_addr())

        self.key_address_puller = self.context.socket(zmq.PULL)
        self.key_address_puller.bind(self.ut.get_key_address_bind_addr())

        self.rid = 0

    def get(self, keys):
        if type(keys) != list:
            keys = [keys]

        worker_addresses = {}
        for key in keys:
            worker_addresses[key] = (self._get_worker_address(key, 1))
        #print("Worker Address: {}".format(worker_addresses[key]))

        if type(worker_addresses[key]) == list:
            worker_addresses[key] = worker_addresses[key][0]

        # Initialize all KV pairs to 0. Only change a value if we get a valid
        # response from the server.
        kv_pairs = {}
        for key in keys:
            kv_pairs[key] = None

        request_ids = []
        for key in worker_addresses:
            if worker_addresses[key]:
                send_sock = self.pusher_cache.get(worker_addresses[key])

                req, _ = self._prepare_data_request([key])
                req.type = GET

                send_request(req, send_sock)
                request_ids.append(req.request_id)

        # Wait for all responses to return.
        responses = recv_response(request_ids, self.response_puller,
                                  KeyResponse)

        for response in responses:
            for tup in response.tuples:
                if tup.invalidate:
                    self._invalidate_cache(tup.key, 'get')

                if tup.error == NO_ERROR and not tup.invalidate:
                    kv_pairs[tup.key] = self._deserialize(tup)

        return kv_pairs

    def get_all(self, keys):
        if type(keys) != list:
            keys = [keys]
            raise ValueError('`get_all` currently only supports single key'
                             + ' GETs.')
        worker_addresses = {}
        for key in keys:
            worker_addresses[key] = self._get_worker_address(key, False)

        # Initialize all KV pairs to 0. Only change a value if we get a valid
        # response from the server.
        kv_pairs = {}
        for key in keys:
            kv_pairs[key] = None

        for key in keys:
            if worker_addresses[key]:
                req, _ = self._prepare_data_request(key)
                req.type = GET

                req_ids = []
                for address in worker_addresses[key]:
                    req.request_id = self._get_request_id()

                    send_sock = self.pusher_cache.get(address)
                    send_request(req, send_sock)

                req_ids.append(req.request_id)

        responses = recv_response(req_ids, self.response_puller, KeyResponse)

        for resp in responses:
            for tup in resp.tuples:
                if tup.invalidate:
                    self._invalidate_cache(tup.key)

                if tup.error == NO_ERROR:
                    val = self._deserialize(tup)

                    if kv_pairs[tup.key]:
                        kv_pairs[tup.key].merge(val)
                    else:
                        kv_pairs[tup.key] = val

        return kv_pairs

    def put(self, key, value):
        port = random.choice(self.elb_ports)
        worker_address = self._query_routing(key, port)
        if type(worker_address) == list:
            worker_address = worker_address[0]

        if not worker_address:
            return False

        send_sock = self.pusher_cache.get(worker_address)

        # We pass in a list because the data request preparation can prepare
        # multiple tuples
        req, tup = self._prepare_data_request([key])
        req.type = PUT

        # PUT only supports one key operations, we only ever have to look at
        # the first KeyTuple returned.
        tup = tup[0]
        tup.payload, tup.lattice_type = self._serialize(value)

        send_request(req, send_sock)
        response = recv_response([req.request_id], self.response_puller,
                                 KeyResponse)[0]

        tup = response.tuples[0]

        if tup.invalidate:
            self._invalidate_cache(tup.key)

        return tup.error == NO_ERROR

    def put_all(self, key, value):
        worker_addresses = self._get_worker_address(key, False)

        if not worker_addresses:
            return False

        req, tup = self._prepare_data_request(key)
        req.type = PUT
        tup.payload, tup.lattice_type = self._serialize(value)
        tup.timestamp = 0

        req_ids = []
        for address in worker_addresses:
            req.request_id = self._get_request_id()

            send_sock = self.pusher_cache.get(address)
            send_request(req, send_sock)

            req_ids.append(req.request_id)

        responses = recv_response(req_ids, self.response_puller, KeyResponse)

        for resp in responses:
            tup = resp.tuples[0]
            if tup.invalidate:
                # reissue the request
                self._invalidate_cache(tup.key)
                return self.durable_put(key, value)

            if tup.error != NO_ERROR:
                return False

        return True

    # Returns the worker address for a particular key. If worker addresses for
    # that key are not cached locally, a query is synchronously issued to the
    # routing tier, and the address cache is updated.
    def _get_worker_address(self, key, access_type, pick=True):
        insert_to_cache = False
        monitor_address = None

        #if it's a GET
        if access_type == 1:
            if key not in self.get_address_cache:
                #Key is not in cache
                port = random.choice(self.elb_ports)
                addresses = self._query_routing(key, port)
                addresses = list(set(addresses))
                for address in addresses:
                    #TODO: Change here the IP to the address of the Master node!
                    if address != "tcp://192.168.0.31:5900":
                        self.get_address_cache[key] = []
                        self.get_address_cache[key].append(address)
                    else:
                        monitor_address = address
                #print(self.get_address_cache)

            if len(self.get_address_cache[key]) == 0:
                return None

            if pick:
                return random.choice(self.get_address_cache[key])
            else:
                return self.get_address_cache[key]

        # if it's a PUT
        if access_type == 2:
            if key not in self.put_address_cache:
                #print("Key is not in cache")
                port = random.choice(self.elb_ports)
                #print("Port: {}".format(port))
                addresses = self._query_routing(key, port)
                addresses = list(set(addresses))
                #print(addresses)
                #print(self.put_address_cache)
                for address in addresses:
                    #TODO: Change here the IP to the address of the Master node!
                    if address != "tcp://192.168.0.31:5900":
                        self.put_address_cache[key] = []
                        self.put_address_cache[key].append(address)
                        insert_to_cache = True
                    else:
                        monitor_address = address
                #print(self.put_address_cache)

            if len(self.put_address_cache[key]) == 0:
                #print('1')
                return None

            if pick:
                #print('2')
                return random.choice(self.put_address_cache[key])
            else:
                #print('3')
                return self.put_address_cache[key]




    # Invalidates the address cache for a particular key when the server tells
    # the client that its cache is out of date.
    def _invalidate_cache(self, key, type='both'):
        if type == 'get':
            del self.get_address_cache[key]
        elif type == 'put':
            del self.put_address_cache[key]
        else:
            try:
                del self.get_address_cache[key]
            except KeyError:
                pass
            try:
                del self.put_address_cache[key]
            except KeyError:
                pass

    # Issues a synchronous query to the routing tier. Takes in a key and a
    # (randomly chosen) routing port to issue the request to. Returns a list of
    # addresses that the routing tier returned that correspond to the input
    # key.
    def _query_routing(self, key, port):
        key_request = KeyAddressRequest()

        key_request.query_type = u'GET'

        key_request.response_address = self.ut.get_key_address_connect_addr()
        key_request.keys.append(key)
        key_request.request_id = self._get_request_id()

        dst_addr = 'tcp://' + self.elb_addr + ':' + str(port)
        send_sock = self.pusher_cache.get(dst_addr)

        send_request(key_request, send_sock)
        response = recv_response([key_request.request_id],
                                 self.key_address_puller,
                                 KeyAddressResponse)[0]

        if response.error != 0:
            return []

        result = []
        for t in response.addresses:
            if t.key == key:
                for a in t.ips:
                    result.append(a)

        return result

    @property
    def response_address(self):
        return self.ut.get_request_pull_connect_addr()
