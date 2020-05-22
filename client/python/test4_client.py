import anna.client as client
import anna.lattices as lattices
import time
import argparse
import random

parser = argparse.ArgumentParser(description='Client for test4. TODO: a description what this "NF" does in the test4.')
parser.add_argument('ip', type=str, help='IP address of the client.')
parser.add_argument('key', type=str, help='Key that is used to save value.')
parser.add_argument('nf_id', type=int, help='ID of the NF')
args = parser.parse_args()


kvs_client = client.AnnaTcpClient(args.ip, args.ip, local=True, offset=int(args.nf_id))

ITER_COUNT = 60
WAIT = 1

READ_BOUND = 260
WRITE_BOUND = 26

CONTROL_FILE = "/hydro/anna/client/python/test4_state.txt"

access_times = []

timestamp = int(time.time()*1000000)
write_counter = 1
str_value = args.ip + "_" + str(args.nf_id) + "_" + str(write_counter)
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
with open(CONTROL_FILE, 'r') as fin:
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
            #print("GET")
            try:
                values = kvs_client.get(args.key)
                #print(values[args.key])
                if values[args.key] != "None":
                    read_num += 1
                    #print("read - {}".format(values[args.key]))
            except UnboundLocalError as e:
                timeout = True
            access_end = time.time()

        # If write
        elif access_type == 1 and write_num <WRITE_BOUND:
            #print("PUT")
            write_counter += 1
            str_value = args.ip + "_" + str(args.nf_id) + "_" + str(write_counter)
            byte_value = str_value.encode()
            timestamp = int(time.time()*1000000)
            lattice_value = lattices.LWWPairLattice(timestamp, byte_value)
            access_start = time.time()
            try:
                success = kvs_client.put(args.key, lattice_value)
                #print(success)
                if not success:
                    print("PUT failed, returned value: {}".format(success))
                if success:
                    write_num += 1
                    #print("write - {}".format(write_num))
            except UnboundLocalError as e:
                timeout = True
            access_end = time.time()

        sec_end = time.time()

    print("READ: {}, WRITE: {}, SUM: {}".format(read_num, write_num, read_num + write_num))
    sum_accesses.append((sec_end, write_num + read_num))

    print("----------------------------------------------------------------------")
    with open(CONTROL_FILE, 'r') as fin:
        state = fin.read()

#print(sum_accesses)

import json
with open('access_time_data_{}.json'.format(args.nf_id), 'w') as f:
    json.dump(sum_accesses, f)

print("DONE")

