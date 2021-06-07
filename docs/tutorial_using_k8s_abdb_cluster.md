In this tutorial, we demonstrate how to put and get key-value pairs from/to annabellaDB cluster and highlight the benefits of AnnaBellaDB. We assume an annabellaDB cluster running in the managed kubernetes system (see the deploy process [here](https://github.com/hsnlab/annabellaDB/blob/master/docs/deploy_in_kubernetes.md)), that containes, beside the master instance, two childrens:
```
$ kubectl get pods -n=annabelladb -o wide
NAME                                             READY   STATUS    RESTARTS   AGE     IP          NODE                 NOMINATED NODE   READINESS GATES
annabelladb-children-daemonset-c4grf             1/1     Running   0          44m     10.1.1.51   kubi-workergop11     <none>           <none>
annabelladb-children-daemonset-tx26d             1/1     Running   0          44m     10.1.2.23   pointcloud-1-gop10   <none>           <none>
annabelladb-master-deployment-7f7c48678f-vsrpm   1/1     Running   0          44m     10.1.1.50   kubi-workergop11     <none>           <none>
```

Conseqently, the k8s cluster looks like this:
![k8s_cluster](https://github.com/hsnlab/annabellaDB/blob/master/docs/k8s_architecture1.png)

It contains two worker nodes, where the pod could run. kubi-workergop11 includes the annabellaDB master and one of the childred DB instance. On the other hand, pointcloud-1-gop10 contains its own annabellaDB child instace with the cluster IP 10.1.2.23.

Let's deploy a standard ubuntu image (with network tools) which will access the annabellaDB cluster in the future:
```
$ cat <<EOF | kubectl apply -n annabelladb -f -
apiVersion: v1
kind: Pod
metadata:
  name: ubuntu
  labels:
    app: ubuntu
spec:
  containers:
  - name: ubuntu
    image: ubuntu:latest
    command: ["/bin/sleep", "3650d"]
    imagePullPolicy: IfNotPresent
  restartPolicy: Always
EOF
```
Now the kubernetes cluster looks like the following:
![k8s_cluster](https://github.com/hsnlab/annabellaDB/blob/master/docs/k8s_architecture2.png)

## PUT a key-value pair into the annabellaDB

Remember, as we mentioned [here](https://github.com/hsnlab/annabellaDB/blob/master/docs/deploy_in_kubernetes.md), all child instance start a HTTP rest API, through which PUT and GET operations can be executed. For this purpose install *curl* HTTP client to the deployed ubuntu pod. 

Open the bash of the pod *ubuntu*:
```
$ kubectl exec -n annabelladb --stdin --tty ubuntu -- /bin/bash
root@ubuntu:/# apt update
...
root@ubuntu:/# apt install curl
...
```

PUT the key *Luis* with value *Torrente* to the annabellaDB. Since the pod *ubuntu* is located on the Node *pointcloud-1-gop10* use the annabellaDB child instance that is deployed in the same node too. In this case this is *annabelladb-children-daemonset-tx26d* with the IP address 10.1.2.23. To execute the PUT:
```
root@ubuntu:/# curl --header "Content-Type: application/json" --request POST --data  '{"key":"Luis", "value":"Torrente" }' http://10.1.2.23:5000/put
"OK"
```

Now the key-value is stored in the annabellaDB or more specifically, in the master instance of the annabelladb cluster. This info can be gathered from the log of the master:
```
$ kubectl exec annabelladb-master-deployment-7f7c48678f-vsrpm -n annabelladb -- tail -f /anna/log_monitoring.txt
[2021-06-07 21:25:15.498] [monitoring_log] [info] DEBUG: *****************************************************************
[2021-06-07 21:25:15.498] [monitoring_log] [info] DEBUG: Current key mapping:
[2021-06-07 21:25:15.498] [monitoring_log] [info] DEBUG:Host 10-1-2-23.annabelladb.pod.cluster.local:
[2021-06-07 21:25:15.498] [monitoring_log] [info] DEBUG:	IP: 10-1-2-23.annabelladb.pod.cluster.local
[2021-06-07 21:25:15.498] [monitoring_log] [info] DEBUG:	Free capacity: 1e+06
[2021-06-07 21:25:15.498] [monitoring_log] [info] DEBUG:	Stored replicas:
[2021-06-07 21:25:15.498] [monitoring_log] [info] DEBUG:Host 10-1-1-51.annabelladb.pod.cluster.local:
[2021-06-07 21:25:15.498] [monitoring_log] [info] DEBUG:	IP: 10-1-1-51.annabelladb.pod.cluster.local
[2021-06-07 21:25:15.498] [monitoring_log] [info] DEBUG:	Free capacity: 1e+06
[2021-06-07 21:25:15.498] [monitoring_log] [info] DEBUG:	Stored replicas:
[2021-06-07 21:25:15.498] [monitoring_log] [info] DEBUG:Host annabelladb-master.default-subdomain.annabelladb.svc.cluster.local:
[2021-06-07 21:25:15.498] [monitoring_log] [info] DEBUG:	IP: annabelladb-master.default-subdomain.annabelladb.svc.cluster.local
[2021-06-07 21:25:15.498] [monitoring_log] [info] DEBUG:	Free capacity: 999976
[2021-06-07 21:25:15.498] [monitoring_log] [info] DEBUG:	Stored replicas:
[2021-06-07 21:25:15.498] [monitoring_log] [info] DEBUG:		 - Luis
[2021-06-07 21:25:15.498] [monitoring_log] [info] DEBUG: *****************************************************************

```
## GET a key-value pair into the annabellaDB

Let's get the stored key *Luis* in the ubuntu pod more frequently by the following commands:
```
# curl --header "Content-Type: application/json" --request POST --data  '{"key":"Luis"}' http://10.1.2.23:5000/get 
"b'Torrente'"root@ubuntu:/# curl --header "Content-Type: application/json" --request POST --data  '{"key":"Luis"}' http://10.1.2.23:5000/get
"b'Torrente'"root@ubuntu:/# curl --header "Content-Type: application/json" --request POST --data  '{"key":"Luis"}' http://10.1.2.23:5000/get
"b'Torrente'"root@ubuntu:/# curl --header "Content-Type: application/json" --request POST --data  '{"key":"Luis"}' http://10.1.2.23:5000/get
"b'Torrente'"root@ubuntu:/# curl --header "Content-Type: application/json" --request POST --data  '{"key":"Luis"}' http://10.1.2.23:5000/get
"b'Torrente'"root@ubuntu:/# curl --header "Content-Type: application/json" --request POST --data  '{"key":"Luis"}' http://10.1.2.23:5000/get
"b'Torrente'"root@ubuntu:/# curl --header "Content-Type: application/json" --request POST --data  '{"key":"Luis"}' http://10.1.2.23:5000/get
"b'Torrente'"root@ubuntu:/# curl --header "Content-Type: application/json" --request POST --data  '{"key":"Luis"}' http://10.1.2.23:5000/get
"b'Torrente'"root@ubuntu:/# curl --header "Content-Type: application/json" --request POST --data  '{"key":"Luis"}' http://10.1.2.23:5000/get
"b'Torrente'"root@ubuntu:/# curl --header "Content-Type: application/json" --request POST --data  '{"key":"Luis"}' http://10.1.2.23:5000/get
"b'Torrente'"root@ubuntu:/# 
```

The AnnaBellaDB have detected the access pattern change of the key *Luis*, i.e., the increased read rate from Node *pointcloud-1-gop10*. To minimize the access time of the stored data, it has reoptimized the location of it, i.e., moved from the annabelladb master DB instace to pod *10-1-2-23.annabelladb.pod.cluster.local* that is run in the same Node *pointcloud-1-gop10* from where the reads are coming from.

```
[2021-06-07 21:28:15.639] [monitoring_log] [info] DEBUG: *****************************************************************
[2021-06-07 21:28:15.639] [monitoring_log] [info] DEBUG: Current key mapping:
[2021-06-07 21:28:15.639] [monitoring_log] [info] DEBUG:Host 10-1-2-23.annabelladb.pod.cluster.local:
[2021-06-07 21:28:15.639] [monitoring_log] [info] DEBUG:	IP: 10-1-2-23.annabelladb.pod.cluster.local
[2021-06-07 21:28:15.639] [monitoring_log] [info] DEBUG:	Free capacity: 999976
[2021-06-07 21:28:15.639] [monitoring_log] [info] DEBUG:	Stored replicas:
[2021-06-07 21:28:15.639] [monitoring_log] [info] DEBUG:		 - Luis
[2021-06-07 21:28:15.639] [monitoring_log] [info] DEBUG:Host 10-1-1-51.annabelladb.pod.cluster.local:
[2021-06-07 21:28:15.639] [monitoring_log] [info] DEBUG:	IP: 10-1-1-51.annabelladb.pod.cluster.local
[2021-06-07 21:28:15.639] [monitoring_log] [info] DEBUG:	Free capacity: 1e+06
[2021-06-07 21:28:15.639] [monitoring_log] [info] DEBUG:	Stored replicas:
[2021-06-07 21:28:15.639] [monitoring_log] [info] DEBUG:Host annabelladb-master.default-subdomain.annabelladb.svc.cluster.local:
[2021-06-07 21:28:15.639] [monitoring_log] [info] DEBUG:	IP: annabelladb-master.default-subdomain.annabelladb.svc.cluster.local
[2021-06-07 21:28:15.639] [monitoring_log] [info] DEBUG:	Free capacity: 999976
[2021-06-07 21:28:15.639] [monitoring_log] [info] DEBUG:	Stored replicas:
[2021-06-07 21:28:15.639] [monitoring_log] [info] DEBUG: *****************************************************************
```

