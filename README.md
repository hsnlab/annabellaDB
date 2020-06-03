# AnnaBellaDB

AnnaBellaDB is a proof-of-concept (PoC) network latency and access-pattern aware key-value store. In our opinion, this solution is ideal to adapt the stateless design and use AnnaBellaDB as a cloud database where the application states could be externalized.

This PoC have been forked from one of the most effective key-value store from the academy, called AnnaDB which was developed in in the [RISE Lab](https://rise.cs.berkeley.edu) at [UC Berkeley](https://berkeley.edu).

More info about AnnaDB can be found at:
* https://github.com/hydro-project/anna
* https://dsf.berkeley.edu/jmh/papers/anna_ieee18.pdf
* https://www.vikrams.io/papers/anna-vldb19.pdf

## Installation

### Installing on your host natively

First of all, you need to install all dependencies of the building process:

```
sudo apt update
cd common
sudo ./scripts/install-dependencies.sh
cd ../client/python
./compile.sh 
```

To build the code, execute the following command in the _/annabellaDB_ directory:
```
bash scripts/build.sh -bRelease -j4
```

The script above is going to generate all binaries into the _build_ directory.

To run a single AnnaBellaDB instance locally, execute in the _/annabellaDB_ directory:
```
./scripts/start-anna-local.sh n y
```
This will open the CLI of the database. Before you are would do anything, please wait at least 20 seconds!! (FIXME: Tackle this BUG).

To write a key/value to the AnnaBellaDB, type the following command in the CLI:
```
PUT <key> <value>
```

Logically, to read the value of a key:
```
GET <key>
```

To STOP AnnaBellaDB, run the following in the _/annabellaDB_ directory of the repository:
```
sudo ./scripts/stop-anna-local.sh y
```


### Installing via Docker

It is possible to create AnnaBellaDB cluster, where the the cluster elements (i.e., the AnnaBella nodes) are Docker containers.
Currently, there is no official AnnaBellaDB container, that is why we are going to use the base container for AnnaDB (https://hub.docker.com/r/hydroproject/base)
and install the auxiliary apps and build the AnnaBellaDB inside it. 

First of all, you need to install docker and other dependencies, if you have not done it yet:
```
sudo apt install docker.io
sudo apt install python-pip
cd /client/python
./compile.sh 
```

Two kinds of AnnaBellaDB instance role exist: i) the bootstrap server and ii) the normal key-value store server. 
For the former the _MasterDockerfile_, while for the latter the _SlaveDockerfile_ is used. Both are located in the _conf_ directory.

In order to create the Docker image of the Bootstrap server, run the following in the _/annabellaDB_ dir:
```
docker build -t master_annabelladb_image -f dockerfiles/MasterDockerfile .
```

Similarly, to create the other key-value store servers, run:
```
docker build -t annabelladb_image -f dockerfiles/SlaveDockerfile .
```

By now, both the master and the slave images are available locally on your host. With the following steps, we can create our own AnnaBellaDB cluster:
* Run an AnnaBella instance with Bootstrap role (from _master_annabelladb_image_)
```
docker run -it -d --name kvs1 master_annabelladb_image
```
* At this moment, the Bootstrap server is waiting for its configuration file. Please edit the the conf/annabella-master-template.yml (change _{DOCKER_IP}_ tags to the IP address of the master docker container launched before) and copy to the container:
```
docker cp conf/annabella-master-template.yml kvs1:/hydro/anna/conf/anna-config.yml
```

* To check if the master is running, e.g, see its monitoring logs:
```
docker exec -it kvs1 tail -f /hydro/anna/log_monitoring.txt 
```

* By now, the bootstrap server is ready, we need to launch the (slave) AnnaBellaDB instances with key-value store role.
```
 docker run -it -d --name kvs2 annabelladb_image
```

* Modify _conf/annabella-slave-template.yml_ ({DOCKER_IP} and {MASTER_DOCKER_IP})

* Copy it to the container:
```
 docker cp conf/annabella-slave-template.yml kvs2:/hydro/anna/conf/anna-config.yml
```

Now a two nodes cluster is running. You can start more slave containers if you want. To get the CLI:
```
docker exec -it kvs1 bash -c "/hydro/anna/build/cli/anna-cli hydro/anna/conf/anna-config.yml
``` 

Please see the following [section](#measuring_scripts), for more details about how to create clusters and run tests on them.

## <a name="measuring_scripts"></a> Performance measurements of AnnaBellaDB

To measure how our AnnaBellaDB solution works differently from other key-value stores, we have created some test scripts
to get a picture about its performance. We have used multiple clients accessing the data which are stored in the AnnaBellaDB cluster.

Our cluster includes 4 AnnaBellaDB instance (_kvs1, kvs2, kvs3, kvs4_). Each of them is able to store data and the placement algorithm
is running on the Bootstrap container, i.e., on _kvs1_.

Install dependencies:
```
sudo apt install python3-pip
sudo pip3 install docker seaborn plotly

# IMPORTANT: enable your user to use docker without sudo!
```

### InterDC Latency Test1

To run this measurement, execute the command from _annabellaDB_ directory:
```
python3 scripts/start_interDC_latency_test1.py
```

To monitor the master node:
```
docker exec -it kvs1 bash -c "tail -f /hydro/anna/log_monitoring.txt"
```


This script performs the followings:
1. Starts Bootstrap server (_kvs1_)
2. Starts the other servers (_kvs2, kvs3, kvs4_)
3. Run [test_client.py](https://github.com/hsnlab/annabellaDB/blob/master/client/python/test_client.py) on _kvs1_. This 
client will PUT a key/value and read it in every sec for a minute. During the process, it measures each access time and 
saves them into a file. 
4. After the client has finished on _kvs1_, run a new one on _kvs2_
5. After the client has finished on _kvs2_, run a new one on _kvs3_
6. After the client has finished on _kvs3_, run a new one on _kvs4_ 
7. Collects the measured access times into a pandas dataframe
8. Depicts it on a plotly plot save as _interDC_latency_test1_annaBellaDB<...>.html_ in _/annabellaDB_ dir
9. Finally, stops the cluster

### InterDC Throughput Test1

To run this measurement, execute the command from _annabellaDB_ directory:
```
python3 scripts/start_interDC_throughput_test1.py
```

This script performs the followings:
1. Starts Bootstrap server (_kvs1_)
2. Starts the other servers (_kvs2, kvs3, kvs4_)
3. Run [max_get_client.py](https://github.com/hsnlab/annabellaDB/blob/master/client/python/max_get_client.py) on _kvs1_. This 
client will PUT a key/value and read it within a sec as much as possible. 
The script run for a minute and finally saves the list of number of accesses during a sec 
into a file. 
4. After the client has finished on _kvs1_, run a new one on _kvs2_
5. After the client has finished on _kvs2_, run a new one on _kvs3_
6. After the client has finished on _kvs3_, run a new one on _kvs4_ 
7. Collects the measured access counts into a pandas dataframe
8. Depicts it on a plotly plot save as _interDC_throughput_test1_annaBellaDB<...>.html_ in _/annabellaDB_ dir
9. Finally, stops the cluster

### InterDC Latency Test2

To run this measurement, execute the command from _annabellaDB_ directory:
```
python3 scripts/start_interDC_latency_test2.py
```

This script performs the followings:
1. Starts Bootstrap server (_kvs1_)
2. Starts the other servers (_kvs2, kvs3, kvs4_)
3. Run [test3_client.py](https://github.com/hsnlab/annabellaDB/blob/master/client/python/test3_client.py) on _kvs1_. This 
client will PUT the key/value one time and read it 10 times in a sec.
4. After 40 seconds, run a new client on _kvs2_
5. After 40 seconds, run a new client on _kvs2_
6. After 40 seconds, run a new client on _kvs3_
7. Repeats step 6 five times
8. Stops all clients
9. Collects the measured access times into a pandas dataframe
10. Depicts it on a plotly plot save as _interDC_latency_test2_annaBellaDB<...>.html_ in _/annabellaDB_ dir
11. Finally, stops the cluster

### InterDC Throughput Test2

To run this measurement, execute the command from _annabellaDB_ directory:
```
python3 scripts/start_interDC_throughput_test2.py y
```

This script performs the followings:
1. Starts Bootstrap server (_kvs1_)
2. Starts the other servers (_kvs2, kvs3, kvs4_)
3. Run [test4_client.py](https://github.com/hsnlab/annabellaDB/blob/master/client/python/test4_client.py) on _kvs1_. This 
client will PUT the key/value 26 times and read it 260 times in a sec.
4. After 40 seconds, run a new client on _kvs2_
5. After 40 seconds, run a new client on _kvs2_
7. After 40 seconds, run a new client on _kvs3_
8. Repeats step 5 five times
9. After 40 seconds, run a new client on _kvs4_
9. Repeats step 8 five times
10. Stops all clients
11. Collects the measured access times into a pandas dataframe
12. Depicts it on a plotly plot and save as _interDC_throughput_test2_annaBellaDB<...>.html_ in _/annabellaDB_ dir
13. Finally, stops the cluster

## TODOs:

* Solve the FIXME/TODO in the source codes
* Tests when the AnnaDB exceed the hot threshold
* look the apache-2.0 license how works
