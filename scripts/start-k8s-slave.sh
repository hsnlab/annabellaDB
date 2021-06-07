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

#cd /hydro/anna
cd /anna

if [ -z "$1" ] && [ -z "$2" ]; then
  echo "Usage: ./$0 build start-user"
  echo ""
  echo "You must run this from the project root directory."
  exit 1
fi

if [ "$1" = "y" ] || [ "$1" = "yes" ]; then
  ./scripts/build.sh
fi

echo "And now we're waiting for the config file..."

# Do not start the server until conf/anna-config.yml has been copied onto this
# pod -- if we start earlier, we won't now how to configure the system.
while [[ ! -f "conf/anna-config.yml" ]]; do
  ip=`ifconfig eth0 | grep "inet " | awk '{print $2}'`
  ip=`echo $ip | tr . -`
  ANNABELLADB_INSTANCE_IP="$ip.annabelladb.pod.cluster.local"
  ANNABELLADB_MASTER_IP="annabelladb-master.default-subdomain.annabelladb.svc.cluster.local"
  echo "The IP of this annabelladb instance is '$ANNABELLADB_INSTANCE_IP'"
  sed -i "s/{DOCKER_IP}/$ANNABELLADB_INSTANCE_IP/" /anna/conf/annabella-slave-template.yml
  sed -i "s/{MASTER_DOCKER_IP}/$ANNABELLADB_MASTER_IP/" /anna/conf/annabella-slave-template.yml
  cp /anna/conf/annabella-slave-template.yml /anna/conf/anna-config.yml
  sleep 5
done

echo "conf/anna-config.yml"

echo "Compile python client package"
cd /anna/client/python || exit
./compile.sh
sleep 1

echo "Start HTTP Rest API server"
cd /anna/client/python || exit
python3 rest_server.py $ANNABELLADB_INSTANCE_IP &
sleep 1

echo "Starting Anna Route daemon..."
cd /anna || exit
./build/target/kvs/anna-route &
RPID=$!

echo "Starting Memory type Anna KVS..."
export SERVER_TYPE="memory"
./build/target/kvs/anna-kvs

