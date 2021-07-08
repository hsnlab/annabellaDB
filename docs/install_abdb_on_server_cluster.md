In this tutorial, we demonstrate how to install AnnaBellaDB (ABDB) and natively run it on your server cluster. We assume that the cluster containes, a master and two children nodes:

![k8s_cluster](https://github.com/hsnlab/annabellaDB/blob/master/docs/cluster_architecture.png)

## Install ABDB master node

First of all, install the dependencies by exectuing the following commands from the root directory of annabellaDB github repo:
```
sudo apt update
cd common
sudo ./scripts/install-dependencies.sh
cd ../client/python
./compile.sh
cd ../..
```

Build the source code of ABDB. Execute the command in the /annabellaDB directory:
```
bash scripts/build.sh -bRelease -j4
```

The [build.sh](https://github.com/hsnlab/annabellaDB/blob/master/scripts/build.sh) script above generatese all binaries into the __build__ directory.

Start the AnnaBellaDB master instance, run the command below in the /annabellaDB directory. The first parameter is the IP address of the master server which is in our case 192.168.0.45. The second parameter should be 'n' or 'y' depending on you'd like to start a CLI client to the ABDB instance or not.
```
./scripts/start-anna-local.sh 192.168.0.45 y
```
The output of this command should be like this:
```
ubuntu@kubi-masterGOP2:~/szalay/annabellaDB$ ./scripts/start-anna-local.sh 192.168.0.45 y
The IP of annabelladb master is 192.168.0.45
Starting Anna Monitor daemon...
Starting Anna Route daemon...
Starting Memory type Anna KVS...
kvs> 
```

If you've started bash CLI client to ABDB, then now you can PUT and GET key-value pairs. E.g., save the key-value: Hose_Luis (key) Torrente (value) and then read it from the ABDB:
```
kvs> 
kvs> PUT Hose_Luis Torrente
Success!
kvs> GET Hose_Luis
Torrente

```

To stop the ABDB instance use the stop-anna-local.sh script:
```
sudo ./scripts/stop-anna-local.sh y
```

## Install ABDB children nodes

Install the dependencies:
```
sudo apt update
cd common
sudo ./scripts/install-dependencies.sh
cd ../client/python
./compile.sh
cd ../..
```

Build the source code:
```
bash scripts/build.sh -bRelease -j4
```

Start the AnnaBellaDB master instance:
  * 1st parameter: IP address of the child server
  * 2nd parameter: IP address of the master server
  * 3rd parameter: 'y' or 'n' to start a bash CLI client to the ABDB or not

```
./scripts/start-local-slave.sh 192.168.0.45 192.168.0.46 y
```

## Stop ABDB instance on a node
```
sudo ./scripts/stop-anna-local.sh  y
```
