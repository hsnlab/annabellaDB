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

if [ "$EUID" -ne 0 ]
  then echo "Please run as root"
  exit
fi

if [ -z "$1" ]; then
  echo "Usage: ./$0 remove-logs"
  exit 1
fi

while IFS='' read -r line || [[ -n "$line" ]] ; do
  kill $line
done < "pids"

for i in $(ps aux | grep anna-monitor | awk '{print $2}');
  do
    sudo kill -9 $i
  done

for i in $(ps aux | grep anna-route | awk '{print $2}');
  do
    sudo kill -9 $i
  done

for i in $(ps aux | grep anna-kvs | awk '{print $2}');
  do
    sudo kill -9 $i
  done


if [ "$1" = "y" ]; then
  rm *log*
fi

rm conf/anna-config.yml
rm pids
