import anna.client as client
import anna.lattices as lattices
import time
import argparse
import random
from influxdb import InfluxDBClient
import json

parser = argparse.ArgumentParser(description='Client to SIGCOMM demo')
parser.add_argument('ip', type=str, help='IP address of this NF client.')
parser.add_argument('influxDB_ip', type=str, help='IP address of the influxDB.')
parser.add_argument('influxDB_port', type=str, help='Port of the influxDB.')
parser.add_argument('key', type=str, help='Key that is used to save value.')
parser.add_argument('nf_id', type=int, help='ID of the NF')
args = parser.parse_args()

kvs_client = client.AnnaTcpClient(args.ip, args.ip, local=True, offset=int(args.nf_id))

influx_client = InfluxDBClient(host=args.influxDB_ip, port=args.influxDB_port)
influx_client.create_database('annabelladb_access')
influx_client.switch_database('annabelladb_access')

access_times = []

# Writing data
timestamp = int(time.time() * 1000000)
access_counter = 1
str_value = args.ip + ":" + str(args.nf_id) + "_" + str(access_counter)
byte_value = str_value.encode()
value = lattices.LWWPairLattice(timestamp, byte_value)
print("First PUT")
try:
    kvs_client.put(args.key, value)
    print("OK")
except UnboundLocalError as e:
    print("TIMEOUT")

##############################################################

time.sleep(1)

while True:

    # Choose random access type: 0 - read, 1 - write
    access_type = random.randint(0, 1)

    if access_type == 0:
        try:
            access_start = time.time()
            values = kvs_client.get(args.key)
            access_end = time.time()
            if values[args.key] != "None":
                print("Successful GET: {} - {}".format(args.key, values[args.key]))
                influx_message_body = [{"measurement": "access_time", "tags": {"access_type": "GET", "NF_ID": "{}_{}".format(args.ip, args.nf_id)},
                                        "fields": {"access_duration": access_end - access_start}}]
                influx_client.write_points(influx_message_body)
        except UnboundLocalError as e:
            print("GET timeout")
    else:
        access_counter += 1
        str_value = args.ip + ":" + str(args.nf_id) + "_" + str(access_counter)
        byte_value = str_value.encode()
        timestamp = int(time.time() * 1000000)
        lattice_value = lattices.LWWPairLattice(timestamp, byte_value)
        try:
            access_start = time.time()
            success = kvs_client.put(args.key, lattice_value)
            access_end = time.time()
            if not success:
                print("PUT failed, returned value: {}".format(success))
            else:
                print("Successful PUT: {}".format(args.key))
                influx_message_body = [{"measurement": "access_time", "tags": {"access_type": "PUT", "NF_ID": "{}_{}".format(args.ip, args.nf_id)},
                                        "fields": {"access_duration": access_end - access_start}}]
                influx_client.write_points(influx_message_body)
        except UnboundLocalError as e:
            print("PUT timeout")

    time.sleep(1)

print("DONE")
