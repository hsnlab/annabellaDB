#!/bin/bash

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

if [ -z "$1" ] && [ -z "$2" ]; then
  echo "Usage: ./$0 <IP of AnnaBellaDB node> start-user"
  echo ""
  echo "You must run this from the project root directory."
  exit 1
fi

ANNABELLADB_MASTER_IP="$1"
echo "The IP of annabelladb master is $ANNABELLADB_MASTER_IP"
cp conf/annabella-master-template.yml conf/anna-config.yml
sed -i "s/{DOCKER_IP}/$ANNABELLADB_MASTER_IP/" conf/anna-config.yml
sleep 2

echo "Starting Anna Monitor daemon..."
./build/target/kvs/anna-monitor &
MPID=$!

echo "Starting Anna Route daemon..."
./build/target/kvs/anna-route &
RPID=$!

echo "Starting Memory type Anna KVS..."
export SERVER_TYPE="memory"
./build/target/kvs/anna-kvs &
SPID=$!

echo $MPID >> pids
echo $RPID >> pids
echo $SPID >> pids

if [ "$2" = "y" ] || [ "$2" = "yes" ]; then
  ./build/cli/anna-cli conf/anna-local.yml
fi
