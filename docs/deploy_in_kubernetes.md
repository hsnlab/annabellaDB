# Deploy annabellaDB cluster in Kubernetes

All the necessary k8s yaml files are located in the [conf](https://github.com/hsnlab/annabellaDB/blob/master/conf)  directory, so execute the commands below from that dir. In this description, we assume you have already installed the kubernetes on your machine (or machine cluster).

Firstly, create a new namespace *annabelladb*. We use deploy all components in there.
```
kubectl create namespace annabelladb
```

## Deploy annabellaDB master instance

Create the master instance and a service related to it with the following command:
```
kubectl apply -f k8s-deployment.yml -n annabelladb
```
The deployment contains the master instance pod. To check if it's running, use this:
```
$ kubectl get pods -n=annabelladb
NAME                                             READY   STATUS    RESTARTS   AGE
annabelladb-master-deployment-7d6d7458ff-k7dpw   1/1     Running   0          41s
```
Now the master instance is waiting for its workers, i.e, the other annabellaDB instances where data could be stored. The master is available at *annabelladb-master.default-subdomain.annabelladb.svc.cluster.local*.
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

The annabellaDB cluster by now contains only the master instance. Let's continue with the child instances.

## Deploy annabellaDB child instances

Again from the [conf](https://github.com/hsnlab/annabellaDB/blob/master/conf), execute the following to deploy a daemonset of annabellaDB child instances:
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

Right now the children and master instances are running in the kubernetes.  To check whether they form a common annabellaDB cluster list the logs of the master instance. It includes the list of annabellaDB instance, the delay matrix among the cluster nodes and the list of stored keys. For example, I got the following log after a while:
```
$ kubectl exec annabelladb-master-deployment-7d6d7458ff-k7dpw -n annabelladb -- tail -f /anna/log_monitoring.txt
[2021-03-03 13:27:11.456] [monitoring_log] [info] DEBUG: Connected KVS servers to the cluster (round 62):
[2021-03-03 13:27:11.456] [monitoring_log] [info] DEBUG:	 - annabelladb-master.default-subdomain.annabelladb.svc.cluster.local:0
[2021-03-03 13:27:11.456] [monitoring_log] [info] DEBUG:	 - 192-168-97-232.annabelladb.pod.cluster.local:0
[2021-03-03 13:27:11.456] [monitoring_log] [info] DEBUG:	 - 192-168-226-127.annabelladb.pod.cluster.local:0
[2021-03-03 13:27:11.456] [monitoring_log] [info] DEBUG:	 - 192-168-133-202.annabelladb.pod.cluster.local:0
```

All children annabellaDB instance contains a HTTP rest API server through which the default PUT and GET data operations (among the others) are available. These servers use the *cluster IP* of the pods and listen on the port 5000. For more details and logs, check the following:

```
$ kubectl logs -n=annabelladb annabelladb-children-daemonset-d5jz9
And now we're waiting for the config file...
The IP of this annabelladb instance is '10-1-2-23.annabelladb.pod.cluster.local'
conf/anna-config.yml
Compile python client package
Start HTTP Rest API server
 * Serving Flask app 'rest_server' (lazy loading)
 * Environment: production
   WARNING: This is a development server. Do not use it in a production deployment.
   Use a production WSGI server instead.
 * Debug mode: off
 * Running on all addresses.
   WARNING: This is a development server. Do not use it in a production deployment.
 * Running on http://10.1.2.23:5000/ (Press CTRL+C to quit)
Starting Anna Route daemon...
Starting Memory type Anna KVS...
```

To see an example, how to use AnnaBellaDB kubernetes cluster and enjoy its benefits check [Tutorial of using k8s annabellaDB cluster](https://github.com/hsnlab/annabellaDB/tree/master/docs/tutorial_using_k8s_abdb_cluster).
