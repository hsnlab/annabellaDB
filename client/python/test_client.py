
import anna.client as client
import anna.lattices as lattices
import time
import argparse

parser = argparse.ArgumentParser(description='Client for test1. TODO: a description what this "NF" does in the test1.')
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
for i in range(ITER_COUNT):
    timeout=""
    before = time.time()
    try:
        values = kvs_client.get(args.key)
    except UnboundLocalError as e:
        timeout = "TIMEOUT"
    after = time.time()
    if timeout == "TIMEOUT":
        print("TIMEOUT")
        values[args.key] = "TIMEOUT"
    print(values[args.key])
    print("\n")
    access_times.append((before, (after - before), str(values[args.key])))
    print(access_times[-1])
    time.sleep(WAIT)

import json
with open('access_time_data.json', 'w') as f:
    json.dump(access_times, f)


print("DONE")