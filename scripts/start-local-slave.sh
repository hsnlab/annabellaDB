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

if [ -z "$1" ] && [ -z "$2" ] && [ -z "$3" ]; then
  echo "Usage: ./$0 <IP of AnnaBellaDB master node> <IP of AnnaBellaDB child node> start-user"
  echo ""
  echo "You must run this from the project root directory."
  exit 1
fi

ANNABELLADB_MASTER_IP="$1"
ANNABELLADB_SLAVE_IP="$2"
echo "The IP of this annabelladb child instance is '$ANNABELLADB_SLAVE_IP'"
echo "The IP of the annabelladb master instance is '$ANNABELLADB_MASTER_IP'"
cp conf/annabella-slave-template.yml conf/anna-config.yml
sed -i "s/{DOCKER_IP}/$ANNABELLADB_SLAVE_IP/" conf/anna-config.yml
sed -i "s/{MASTER_DOCKER_IP}/$ANNABELLADB_MASTER_IP/" conf/anna-config.yml
sleep 2


echo "Starting Anna Route daemon..."
./build/target/kvs/anna-route &
RPID=$!

echo "Starting Memory type Anna KVS..."
export SERVER_TYPE="memory"
./build/target/kvs/anna-kvs &
SPID=$!

if [ "$3" = "y" ] || [ "$3" = "yes" ]; then
  ./build/cli/anna-cli conf/anna-config.yml
fi
