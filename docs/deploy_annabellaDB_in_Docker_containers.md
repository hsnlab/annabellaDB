### Install AnnaBellaDB cluster in Docker containers

It is possible to create AnnaBellaDB cluster, where the the cluster elements (i.e., the AnnaBella hosts) are Docker containers.
Currently, there is no official AnnaBellaDB container, that is why we are going to use the base container for AnnaDB (https://hub.docker.com/r/hydroproject/base) to install the auxiliary apps and build the AnnaBellaDB inside that. 

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
docker run -it -d --name annabelladb_master master_annabelladb_image
```
* At this moment, the Bootstrap server is waiting for its configuration file. Please edit the the conf/annabella-master-template.yml (change _{DOCKER_IP}_ tags to the IP address of the master docker container launched before) and copy to the container.
    * To get the container's IP use this command:
        ```
        docker inspect -f '{{range.NetworkSettings.Networks}}{{.IPAddress}}{{end}}' annabelladb_master
        ```
    * Modify cofiguration file and copy it into the container:
        ```
        docker cp conf/annabella-master-template.yml annabelladb_master:/anna/conf/anna-config.yml
        ```

* To check if the master is running, e.g, see its monitoring logs:
```
docker exec -it annabelladb_master tail -f /anna/log_monitoring.txt 
```

* By now, the bootstrap server is ready, we need to launch the (slave) AnnaBellaDB instances with key-value store role.
```
 docker run -it -d --name annabelladb_child1 annabelladb_image
```

* Modify _conf/annabella-slave-template.yml_ ({DOCKER_IP} and {MASTER_DOCKER_IP})

* Copy it to the container:
```
 docker cp conf/annabella-slave-template.yml annabelladb_child1:/anna/conf/anna-config.yml
```

Now a two nodes cluster is running. You can start more slave containers if you want. To get the CLI:
```
docker exec -it annabelladb_master bash -c "/anna/build/cli/anna-cli /anna/conf/anna-config.yml
