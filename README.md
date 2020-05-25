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
TODO
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

Two kinds of AnnaBellaDB instance role exist: i) the bootstrap server and ii) the normal key-value store server. 
For the former the _MasterDockerfile_, while for the latter the _SlaveDockerfile_ is used. Both are located in the _conf_ directory.

In order to create the Docker image of the Bootstrap server, run the following in the _/annabellaDB_ dir:
```
docker build -t master_annabelladb_image -f dockerfiles/MasterDockerfile .
```

Similarly, to create the other key-value store servers, run:
```
docker build -t slave_annabelladb_image -f dockerfiles/SlaveDockerfile .
```

By now, both the master and the slave images are available locally on your host. With the following steps, we can create our own AnnaBellaDB cluster:
* Run an AnnaBella instance with Bootstrap role (from _master_annabelladb_image_)
```
docker run -it -d --name kvs1 master_annabelladb_image
```
* At this moment, the Bootstrap server is waiting for its configuration file. Please edit the the conf/annabella-master-template.yml (change _{DOCKER_IP}_ tags to the IP address of the master docker container launched before) and copy to the container:
```
docker cp conf/annabella-master-template.yml kvs1:/hydro/anna/conf/anna-config.yml"
```

* To check if the master is running, e.g, see its monitoring logs:
```
docker exec -it kvs1 tail -f /hydro/anna/log_monitoring_0.txt 
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
TODO

## TODOs:


