In this description, we are going to show, how you could ABDB in your python application, and enjoy the benefits offered by it. 

We assume that you already create and run your ABDB server cluster, so by now everything is ready for use :) 
To see how to create ABDB server cluster, please read this: [install ABDB on your server cluster](https://github.com/hsnlab/annabellaDB/blob/master/docs/install_abdb_on_server_cluster.md)

# Install python client for ABDB

```
 cd client/python/
 ./compile.sh
 ./install_dependencies.sh
 sudo python setup.py install
```

Now you're ready to use ABDB from your python source code. In the followings we show how cloud you import ABDB client and PUT or GET key-value pairs to your ABDB cluster.

# How to use python client for ABDB

**Import anna client and connect to ABDB**
```
import anna.client as client
import anna.lattices as lattices

kvs_client = client.AnnaTcpClient(<IP of the 'nearest' ABDB instance>, <IP of the 'nearest' ABDB instance>, local=True, offset=int(1))
```
Use always the nearest AnnaBellaDB instance to connect, due to it will optimize the location according the to client access numbers. E.g., If you followed our tutorial [here](https://github.com/hsnlab/annabellaDB/blob/master/docs/install_abdb_on_server_cluster.md), you should have one master and two childen servers. If you run your python program in the master, use _192.168.0.45_ IP address, while in case of child1 or child2 use _192.168.0.46_ or _192.168.0.47_.

**PUT key-value pair to the ABDB store**
```
key = "KEY"

# Create timestamp to the PUT operation
timestamp = int(time.time() * 1000000)

# Define the value to save
str_value = "VALUE"

# Encode it to bytes
byte_value = str_value.encode()

# Convert it to lattice type
value = lattices.LWWPairLattice(timestamp, byte_value)

# PUT key-value pair
try:
    kvs_client.put(key, value)
    print("OK")
except UnboundLocalError as e:
    print("TIMEOUT")
```

**GET value from the ABDB store**
```
values = kvs_client.get(key)
value = values[key]
```

# Test benchmarking tool for your ABDB store.

The [test_benchmarking.py](https://github.com/hsnlab/annabellaDB/blob/master/client/python/test_benchmarking.py) is a basic benchmark python tool, that reads and writes 100 times a key-value pair to your ABDB cluster and measures the necesary time to perform these operations. 

To start the benchmark tool, use the command below, where the parameter is the IP address of the nearest ABDB instance. E.g., in this case we run it on the child2 server, that's why the _192.168.0.47_ is used.
```
./test_benchmarking.py 192.168.0.47
```

When the benchmark finishes, a plot will be generated about the measured values, similiar like this:
![benchmark](https://github.com/hsnlab/annabellaDB/blob/master/docs/worker2.png)

In this Figure, we can conclude that the PUT operation is about 0.8 _ms_. However, in case of the GET, we can split the measured values into two phases: one before the data location optimization, and one after that. At the begging, the requested data is stored at the Master ABDB instance, so we get higher access times. After ABDB migrates (at aroung 20th iteration) the data to the child2 server, where the benchmark tool is running, the access times drop down to 0.5 _ms_. 
