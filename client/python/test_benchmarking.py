#!/usr/bin/python3

import anna.client as client
import anna.lattices as lattices
import time
import argparse
import random
import json
import seaborn as sns
import pandas as pd
import matplotlib.pyplot as plt

parser = argparse.ArgumentParser(description='Basic benchmark to measure PUT and GET times')
parser.add_argument('ip', type=str, help='IP address of the "nearest" ABDB instance')
args = parser.parse_args()

kvs_client = client.AnnaTcpClient(args.ip, args.ip, local=True, offset=int(1))

KEY="szalay"

### PUT data

# Create timestamp to the PUT operation
timestamp = int(time.time() * 1000000)

# Define the value to save
str_value = args.ip + ":TEST_VALUE"

# Encode it to bytes
byte_value = str_value.encode()

# Convert it to lattice type
value = lattices.LWWPairLattice(timestamp, byte_value)

print("--- PUT")
# PUT key-value pair
try:
    kvs_client.put(KEY, value)
    print("OK")
except UnboundLocalError as e:
    print("TIMEOUT")

##############################################################

time.sleep(1)
access_times=[]
df = pd.DataFrame()

print("--- GET")

for i in range(100):
    try:
        access_start = time.time()
        values = kvs_client.get(KEY)
        access_end = time.time()

        value = values[KEY]
        #print(value)
        access_time = (access_end-access_start)*1000
        print("Necessary time for GET: {}".format(access_time))
        #access_times.append(access_time)
        df = df.append({'iteration':i, 'operation_time[ms]':access_time, 'operation':'GET'}, ignore_index=True)
    except UnboundLocalError as e:
        print("GET timeout")
    time.sleep(0.5)

###############################################################

print("--- PUT")
# PUT key-value pair

put_times=[]

for i in range(100):
    try:

        # Create timestamp to the PUT operation
        timestamp = int(time.time() * 1000000)
        # Define the value to save
        str_value = args.ip + ":TEST_VALUE{}".format(i)
        # Encode it to bytes
        byte_value = str_value.encode()
        # Convert it to lattice type
        value = lattices.LWWPairLattice(timestamp, byte_value)

        access_start = time.time()
        kvs_client.put(KEY, value)
        access_end = time.time()

        access_time = (access_end-access_start)*1000
        print("Necessary time for PUT: {}".format(access_time))
        put_times.append(access_time)
        df = df.append({'iteration':i, 'operation_time[ms]':access_time, 'operation':'PUT'}, ignore_index=True)
    except UnboundLocalError as e:
        print("TIMEOUT")
    time.sleep(0.5)

###############################################################
print(df)
sns.lineplot(data=df, x="iteration", y="operation_time[ms]", hue="operation")
plt.show()
