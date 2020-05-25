import anna.client as client
import anna.lattices as lattices
import time
from datetime import datetime
import argparse
from multiprocessing import Process
import random

parser = argparse.ArgumentParser(description='Client to SIGCOMM demo')
parser.add_argument('ip', type=str, help='IP address of the client.')
parser.add_argument('key', type=str, help='Key that is used to save value.')
parser.add_argument('nf_id', type=int, help='ID of the NF')
args = parser.parse_args()


kvs_client = client.AnnaTcpClient(args.ip, args.ip, local=True, offset=int(args.nf_id))

ITER_COUNT = 60
WAIT = 1

READ_BOUND = 10
WRITE_BOUND = 1

access_times = []

timestamp = int(time.time()*1000000)
write_counter = 1
str_value = args.ip + "_" + str(write_counter)
byte_value = str_value.encode()
print(byte_value)
value = lattices.LWWPairLattice(timestamp, byte_value)
print("PUT")
try:
    kvs_client.put(args.key, value)
    print("OK")
except UnboundLocalError as e:
    print("TIMEOUT")

##############################################################

time.sleep(2)

print("GET")

state = None
with open('/hydro/anna/client/python/test3_state.txt', 'r') as fin:
    state = fin.read()

sum_accesses = []

while state != "STOP\n":
    print(state)
    sec_start = time.time()
    sec_end = time.time()
    write_num = 0
    read_num = 0
    while (sec_end - sec_start) < 1:
        timeout = False

        access_type = random.randint(0, 1)

        # If read
        if access_type == 0 and read_num < READ_BOUND:
            access_start = time.time()
            try:
                values = kvs_client.get(args.key)
                if values[args.key] != "None":
                    read_num += 1
                print("read - {}".format(read_num))
            except UnboundLocalError as e:
                timeout = True
            access_end = time.time()
            if values[args.key] != "None":
                sum_accesses.append((access_end, access_end-access_start, access_type))

        # If write
        elif access_type == 1 and write_num <WRITE_BOUND:
            write_counter += 1
            str_value = args.ip + "_" + str(write_counter)
            byte_value = str_value.encode()
            print(byte_value)
            timestamp = int(time.time()*1000000)
            lattice_value = lattices.LWWPairLattice(timestamp, byte_value)
            access_start = time.time()
            success = False
            try:
                success = kvs_client.put(args.key, lattice_value)
                if not success:
                    print("PUT failed, returned value: {}".format(success))
                write_num += 1
                print("write - {}".format(write_num))
            except UnboundLocalError as e:
                timeout = True
            access_end = time.time()
            if success:
                sum_accesses.append((access_end, access_end-access_start, access_type))

        sec_end = time.time()

    print("----------------------------------------------------------------------")
    with open('/hydro/anna/client/python/test3_state.txt', 'r') as fin:
        state = fin.read()

#print(sum_accesses)

import json
with open('access_time_data_{}.json'.format(args.nf_id), 'w') as f:
    json.dump(sum_accesses, f)

print("DONE")

