# Deploy annabellaDB cluster in Kubernetes

All the necessary yaml files are located in the conf directory, so execute the followings from there. I assume you have already installed the kubernetes on your machine (or machine cluster).

Firstly, create a new namespace *annabelladb*. We use this to deploy all components here.
```
kubectl create namespace annabelladb
```

## Deploy and configure annabellaDB master instance

Create the master instance and a service for it with the following command:
```
kubectl apply -f k8s-deployment.yml -n annabelladb
```
The deployment contains the master instance pod. To check if it's running use this:
```
$ kubectl get pods -n=annabelladb
NAME                                             READY   STATUS    RESTARTS   AGE
annabelladb-master-deployment-7d6d7458ff-k7dpw   1/1     Running   0          41s
```
Now the master instance is waiting for its configuration. 
```
$ kubectl logs annabelladb-master-deployment-7d6d7458ff-k7dpw -n=annabelladb
And now we're waiting for the config file...

```
Don't worry, nothing serious, we just need to set its IP as an environment variable. With the *k8s-deployment.yml* file, we also created a headless service to the master, so now it is available with the domain name *annabelladb-master.annabelladb-master-service.annabelladb.svc.cluster.local* . Consequently, inside the master instance's pod insert the line below to its */etc/environment*:
```
ANNABELLADB_MASTER_IP="annabelladb-master.annabelladb-master-service.annabelladb.svc.cluster.local"
```
To check it is working now see its logs:
```
$ kubectl logs annabelladb-master-deployment-7d6d7458ff-k7dpw -n=annabelladb
And now we're waiting for the config file...
The IP of annabelladb master is annabelladb-master.default-subdomain.annabelladb.svc.cluster.local
conf/anna-config.yml
Starting Anna Monitor daemon...
Starting Anna Route daemon...
Starting Memory type Anna KVS...
```

You can check the annabellaDB's logs:
```
$ kubectl exec annabelladb-master-deployment-7d6d7458ff-k7dpw -n annabelladb -- tail -f /anna/log_monitoring.txt

[2021-03-03 13:15:52.812] [monitoring_log] [info] DEBUG: Connected KVS servers to the cluster (round 17):
[2021-03-03 13:15:52.812] [monitoring_log] [info] DEBUG:	 - annabelladb-master.default-subdomain.annabelladb.svc.cluster.local:0
[2021-03-03 13:15:52.812] [monitoring_log] [info] DEBUG: *****************************************************************
[2021-03-03 13:15:52.812] [monitoring_log] [info] DEBUG: Content of the key_replication_map
[2021-03-03 13:15:52.812] [monitoring_log] [info] DEBUG: *****************************************************************
[2021-03-03 13:15:52.812] [monitoring_log] [info] DEBUG: Current key mapping:
[2021-03-03 13:15:52.812] [monitoring_log] [info] DEBUG:Host annabelladb-master.default-subdomain.annabelladb.svc.cluster.local:
[2021-03-03 13:15:52.812] [monitoring_log] [info] DEBUG:	IP: annabelladb-master.default-subdomain.annabelladb.svc.cluster.local
[2021-03-03 13:15:52.812] [monitoring_log] [info] DEBUG:	Free capacity: 1e+06
[2021-03-03 13:15:52.812] [monitoring_log] [info] DEBUG:	Stored replicas:
[2021-03-03 13:15:52.812] [monitoring_log] [info] DEBUG: *****************************************************************
[2021-03-03 13:15:52.812] [monitoring_log] [info] DEBUG: Writing rates:
[2021-03-03 13:15:52.812] [monitoring_log] [info] DEBUG: Reading rates:
[2021-03-03 13:15:52.812] [monitoring_log] [info] DEBUG: *****************************************************************
[2021-03-03 13:15:52.812] [monitoring_log] [info] DEBUG: Delay matrix:
[2021-03-03 13:15:52.812] [monitoring_log] [info] DEBUG:	 [annabelladb-master.default-subdomain.annabelladb.svc.cluster.local][annabelladb-master.default-subdomain.annabelladb.svc.cluster.local] = 0
[2021-03-03 13:15:52.812] [monitoring_log] [info] DEBUG: -----------------------------------------------------------------
```

Now the annabellaDB cluster for now contains only the master instance. Let's continue with the slave instances.

## Deploy annabellaDB child instances

Again from the */conf*, execute the following to deploy a daemonset of annabellaDB child instances:
```
$ kubectl apply -f k8s-daemonset.yml -n annabelladb
```
Check the running pods:
```
$ kubectl get pods -n=annabelladb
NAME                                             READY   STATUS    RESTARTS   AGE
annabelladb-children-daemonset-d5jz9             1/1     Running   0          79s
annabelladb-children-daemonset-qjsf8             1/1     Running   0          79s
annabelladb-children-daemonset-rwrpk             1/1     Running   0          79s
annabelladb-master-deployment-7d6d7458ff-k7dpw   1/1     Running   0          20m
```

Right now the children and master instances are running in the kubernetes.  To check whether they form a common annabellaDB cluster list the logs of the master instance. It includes the list of annabellaDB instance, the delay matrix among the cluster nodes and the list of stored keys. For example, I got the following log:
```
$ kubectl exec annabelladb-master-deployment-7d6d7458ff-k7dpw -n annabelladb -- tail -f /anna/log_monitoring.txt
[2021-03-03 13:27:11.456] [monitoring_log] [info] DEBUG: Connected KVS servers to the cluster (round 62):
[2021-03-03 13:27:11.456] [monitoring_log] [info] DEBUG:	 - annabelladb-master.default-subdomain.annabelladb.svc.cluster.local:0
[2021-03-03 13:27:11.456] [monitoring_log] [info] DEBUG:	 - 192-168-97-232.annabelladb.pod.cluster.local:0
[2021-03-03 13:27:11.456] [monitoring_log] [info] DEBUG:	 - 192-168-226-127.annabelladb.pod.cluster.local:0
[2021-03-03 13:27:11.456] [monitoring_log] [info] DEBUG:	 - 192-168-133-202.annabelladb.pod.cluster.local:0
```

Now we can sure that the desired cluster is working. To check an application example how to use annabellaDB to store the internal data see TODO.



