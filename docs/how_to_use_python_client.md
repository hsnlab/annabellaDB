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


