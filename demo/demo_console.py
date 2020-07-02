import shlex
from cmd import Cmd
import subprocess
import time
import docker
import networkx as nx
import matplotlib.pyplot as plt
import copy
import _thread
import os

G = nx.Graph()
SERVER_IP = {"kvs1": "172.17.0.2", "kvs2": "172.17.0.3"}
nf_server_assignment = {}
client = docker.from_env(timeout=180)

kvs1_exist = False
data_location = None
def execute_bash_command(command):
    bashCommand = command
    process = subprocess.Popen(bashCommand.split(), stdout=subprocess.PIPE)
    output, error = process.communicate()
    # if output != "":
    #    print("Command: '{}' gives:".format(command))
    #    print(output)
    if error != None:
        print("Command: '{}' gives:".format(command))
        print("ERROR: {}".format(error))

def refresh_graph():
    color_map = []
    node_size_map = []
    for node in G:
        if "NF1" in node:
            color_map.append('green')
            node_size_map.append(1000)
        elif "NF2" in node:
            color_map.append('yellow')
            node_size_map.append(1000)
        elif "NF3" in node:
            color_map.append('blue')
            node_size_map.append(1000)
        elif "data" in node:
            color_map.append('red')
            node_size_map.append(500)
        else:
            color_map.append('grey')
            node_size_map.append(2000)

    nx.draw(G, node_color=color_map, node_size=node_size_map, with_labels=True, font_weight='bold')
    plt.savefig("Topology.jpg", format="JPG")
    plt.clf()

    #execute_bash_command("docker cp Topology.jpg dockergrafanainfluxkit_grafana_1:/usr/share/grafana/public/img/.")

def detect_states():

    global kvs1_exist
    global data_location
    global G
    while not kvs1_exist:
        time.sleep(3)

    prev_location = None
    while True:
        execute_bash_command("docker cp kvs1:/hydro/anna/log_monitoring.txt . ")
        data_location_ip = "not_known"
        with open("log_monitoring.txt", "r") as file:
            for line in file:
                if "Key" in line and "Master" in line and "Slave" in line:
                    data_location_ip = line.split()[10][1:-1]
            #print(data_location_ip)
            for k, v in SERVER_IP.items():
                if v == data_location_ip:
                    data_location = k
                    #print(data_location)
                    if prev_location != data_location:
                        try:
                            G.remove_node("data")
                        except Exception:
                            pass
                        G.add_node("data")
                        G.add_edge("data", data_location)
                        refresh_graph()
                        prev_location = data_location


        time.sleep(3)
    print("log exit")


class MyPrompt(Cmd):

    def emptyline(self):
        if self.lastcmd:
            self.lastcmd = ""
            return self.onecmd('\n')

    def do_exit(self, inp):
        print("Bye")
        return True

    def do_start_cluster(self, inp):

        global data_location
        global G

        print("Starting InfluxDB and Grafana... ")
        execute_bash_command("docker-compose -f ../../DockerGrafanaInfluxKit/docker-compose.yml up -d")

        print("Starting AnnabellaDB Bootstrap server...")
        execute_bash_command("docker run -it -d --name kvs1 master_annabelladb_image")
        execute_bash_command("docker exec --privileged kvs1 tc qdisc replace dev eth0 root netem delay 500ms")
        print("\tLoading config file...")
        execute_bash_command("docker cp ../conf/test.yml kvs1:/hydro/anna/conf/anna-config.yml")

        print("Starting AnnabellaDB Server...")
        execute_bash_command("docker run -it -d --name kvs2 annabelladb_image")
        execute_bash_command("docker exec --privileged kvs2 tc qdisc replace dev eth0 root netem delay 500ms")
        print("\tLoading config file...")
        execute_bash_command("docker cp ../conf/test-slave.yml kvs2:/hydro/anna/conf/anna-config.yml")

        print(
            "Grafana dashboard is available on: \n\thttp://localhost:3000/dashboard/db/access-times-of-nfs?refresh=5s&orgId=1")

        G.add_nodes_from(["kvs1", "kvs2"])
        G.add_edge("kvs1", "kvs2")
        refresh_graph()

        global kvs1_exist
        kvs1_exist = True
        time.sleep(5)

    def do_delete_cluster(self, inp):
        global G
        print("Deleting InfluxDB and Grafana... ")
        execute_bash_command("docker rm -f dockergrafanainfluxkit_influxdb_1")
        print("Deleting Grafana... ")
        execute_bash_command("docker rm -f dockergrafanainfluxkit_grafana_1")
        print("Deleting AnnaBellaDB cluster... ")
        execute_bash_command("docker rm -f kvs1")
        execute_bash_command("docker rm -f kvs2")

        nodes = copy.deepcopy(G.nodes())
        for node in nodes:
            G.remove_node(node)
        refresh_graph()

    def do_start_NF(self, inp):
        try:
            params = inp.split(' ')
            nf_id = params[0]
            nf = "NF{}".format(nf_id)
            print("NF ID: {}".format(nf))

            server = params[1]
            print("Server: {}".format(server))

            print("Starting {} on {}...".format(nf, server))
            container = client.containers.get(server)
            # docker_ip_add = container.attrs['NetworkSettings']['IPAddress']
            container.exec_run(
                '/KVS-CLIENT/bin/python3 /hydro/anna/client/python/demoNF.py {} 172.17.0.1 8086 a {}'.format(
                    SERVER_IP[server], nf_id), detach=True)

            nf_server_assignment[nf] = server

            G.add_node(nf)
            G.add_edge(nf, server)
            refresh_graph()

        except IndexError:
            print("Invalid command. To use: start_NF <NF ID> <server container name>")
        except docker.errors.NotFound:
            print("There is no '{}' container".format(nf))
        # execute_bash_command("docker exec -it -d {} bash -c '/KVS-CLIENT/bin/python3 /hydro/anna/client/python/demoNF.py {} 172.17.0.1 8086 a {}'".format(server, SERVER_IP[server], nf_id))
        # print('docker exec -it -d {} bash -c "/KVS-CLIENT/bin/python3 /hydro/anna/client/python/demoNF.py {} 172.17.0.1 8086 a {}"'.format(server, SERVER_IP[server], nf_id))

    def do_delete_NF(self, inp):
        global G
        try:
            params = inp.split(' ')
            nf_id = params[0]

            container = client.containers.get(nf_server_assignment[nf_id])
            output = container.exec_run(['sh', '-c', 'ps aux | grep  demoNF'], stderr=True, stdout=True)
            output = output.output.decode("utf-8")
            for line in output.split("\n"):
                if line.split(' ')[-1] == nf_id[2:]:
                    line = line.split(' ')
                    line = [i for i in line if i != '']
                    pid = line[1]
                    container.exec_run(['kill', '-9', pid])
                    break

            G.remove_node(nf_id)
            refresh_graph()

        except KeyError:
            print("There is no NF such {}".format(nf_id))


_thread.start_new_thread(detect_states, ())
MyPrompt().cmdloop()
print("after")
