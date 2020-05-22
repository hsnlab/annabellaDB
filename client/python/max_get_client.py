
import anna.client as client
import anna.lattices as lattices
import time
import argparse

parser = argparse.ArgumentParser(description='Client for test2. TODO: a description what this "NF" does in the test2.')
parser.add_argument('ip', type=str, help='IP address of the client.')
parser.add_argument('key', type=str, help='Key that is used to save value.')
args = parser.parse_args()


kvs_client = client.AnnaTcpClient(args.ip, args.ip, local=True)

ITER_COUNT = 60
WAIT = 1

access_times = []

timestamp = int(time.time()*1000000)
value = lattices.LWWPairLattice(timestamp, b'1')
print("PUT")
try:
    kvs_client.put(args.key, value)
    print("OK")
except UnboundLocalError as e:
    print("TIMEOUT")

##############################################################

time.sleep(2)

print("GET")

start_time = time.time()
end_time = time.time()

sum_accesses = []

while (end_time - start_time) < ITER_COUNT:
    sec_start = time.time()
    sec_end = time.time()
    get_access_count = 0
    while (sec_end - sec_start) < 1:
        timeout = False
        try:
            values = kvs_client.get(args.key)
        except UnboundLocalError as e:
            timeout = True
        #print(values[args.key])
        if values[args.key] != "None" and not timeout:
            get_access_count += 1
        sec_end = time.time()
    sum_accesses.append((sec_end, get_access_count))
    end_time = time.time()

print(sum_accesses)

import json
with open('access_time_data.json', 'w') as f:
    json.dump(sum_accesses, f)

print("DONE")