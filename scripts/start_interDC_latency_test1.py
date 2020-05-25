LOCAL_ANNA_DIR = "."
LOCAL_IP = "172.17.0.1"

from datetime import datetime
import docker
import subprocess
from shutil import copyfile
import time
import sys
import json
import seaborn as sns;

sns.set()
import matplotlib.pyplot as plt
import plotly.express as px
import plotly.graph_objects as go

client = docker.from_env(timeout=180)

containers = ['kvs2', 'kvs3', 'kvs4']
all_containers = ['kvs1'] + containers


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

##################################################################################################################
### Start Master KVS (Monitor, Server, Router)
# Delete container if already exist
container_name = 'kvs1'
print("Delete {} container if already exist".format(container_name))
try:
    cont = client.containers.get(container_name)
    cont.remove(force=True)
except Exception as e:
    print(e)
print("Start Master KVS...")

# start master kvs
print("docker run -it -d --name {} master_annabelladb_image".format(container_name))
execute_bash_command("docker run -it -d --name {} master_annabelladb_image".format(container_name))
time.sleep(1)

# Creating the config file of docker KVS
container = client.containers.get(container_name)
MASTER_IP = container.attrs['NetworkSettings']['IPAddress']

with open('{}/conf/annabella-master-template.yml'.format(LOCAL_ANNA_DIR), "rt") as fin:
    with open('{}/conf/annabella-{}.yml'.format(LOCAL_ANNA_DIR, container_name), "wt") as fout:
        for line in fin:
            fout.write(line.replace('{DOCKER_IP}', MASTER_IP))

# Copying configuration file to the container
print("docker cp conf/annabella-{0}.yml {0}:/hydro/anna/conf/anna-config.yml".format(container_name))
execute_bash_command("docker cp conf/annabella-{0}.yml {0}:/hydro/anna/conf/anna-config.yml".format(container_name))

##################################################################################################################
### Start slave KVS
for id in containers:

    container_name = id

    print("\nDelete {} container if already exist".format(container_name))
    try:
        cont = client.containers.get(container_name)
        cont.remove(force=True)
    except Exception as e:
        print(e)

    print("Start Slave Docker {}...".format(container_name))

    # Starting KVS docker container
    execute_bash_command("docker run -it -d --name {} annabelladb_image".format(container_name))
    time.sleep(1)

    # Creating the config file of docker KVS
    container = client.containers.get(container_name)
    docker_ip_add = container.attrs['NetworkSettings']['IPAddress']

    with open('{}/conf/annabella-slave-template.yml'.format(LOCAL_ANNA_DIR), "rt") as fin:
        with open('{}/conf/annabella-{}.yml'.format(LOCAL_ANNA_DIR, container_name), "wt") as fout:
            for line in fin:
                line = line.replace('{DOCKER_IP}', docker_ip_add)
                line = line.replace('{MASTER_DOCKER_IP}', MASTER_IP)
                fout.write(line)
    
    print("docker cp conf/annabella-{0}.yml {0}:/hydro/anna/conf/anna-config.yml".format(container_name))
    execute_bash_command("docker cp conf/annabella-{0}.yml {0}:/hydro/anna/conf/anna-config.yml".format(container_name))

    # Set delay to the container's interface
    delay = (int(container_name[-1:]) - 1) * 5
    print("docker exec --privileged {} tc qdisc replace dev eth0 root netem delay {}ms".format(container_name,
                                                                                             delay))
    execute_bash_command(
        "docker exec --privileged {} tc qdisc replace dev eth0 root netem delay {}ms".format(container_name,
                                                                                             delay))
    # Start KVS
    # execute_bash_command('docker exec -i -d {} /hydro/anna/scripts/start-anna-docker.sh n n&'.format(container_name))
    print("\tTo get {} client:".format(container_name))
    print('\t\tdocker exec -it {} bash -c "/hydro/anna/build/cli/anna-cli hydro/anna/conf/anna-config.yml"'.format(
        container_name))

print("\nWaiting for cluster initialization...")
time.sleep(20)


##################################################################################################################
for id in all_containers:
    container_name = id
    print("\nStart Client on docker container {} ...".format(container_name))
    container = client.containers.get(container_name)
    docker_ip_add = container.attrs['NetworkSettings']['IPAddress']

    kvs_container = client.containers.get(container_name)
    print("/KVS-CLIENT/bin/python3 /hydro/anna/client/python/test_client.py  {} b".format(docker_ip_add))
    kvs_container.exec_run(
        "/KVS-CLIENT/bin/python3 /hydro/anna/client/python/test_client.py  {} b".format(docker_ip_add), stderr=True,
        stdout=True)

    print("docker cp {0}:/access_time_data.json {1}/access_pattern_results/{0}_access_pattern.json".format(
            container_name, LOCAL_ANNA_DIR))
    execute_bash_command(
        "docker cp {0}:/access_time_data.json {1}/access_pattern_results/{0}_access_pattern.json".format(
            container_name, LOCAL_ANNA_DIR))

# Collect data

##################################################################################################################

print("\nLoading Access time files...")

import pandas as pd

df = pd.DataFrame(columns=['client', 'access_time', 'access_duration'])

# last_time = 0
for kvs in all_containers:
    print("\t{}...".format(kvs))
    try:
        with open('access_pattern_results/{}_access_pattern.json'.format(kvs)) as json_file:
            localdata = json.load(json_file)

        start_time = None

        for tuple in localdata:
            df = df.append({'client': kvs, "access_time": tuple[0], "access_duration": tuple[1]}, ignore_index=True)
        # last_time = localdata[-1][0]-start_time+1
    except Exception as e:
        print(e)

print(df.head())
print(df.tail())

# scatter(x=list(df.access_time), y=list(df.access_duration))
# fig.show()

fig = go.Figure()

for i in all_containers:
    df_filtered = df[df.client.eq(i)]
    print(i)
    print(df_filtered.head())
    fig.add_scatter(x=df_filtered.access_time, y=df_filtered.access_duration, name=i, mode='markers')

fig.update_layout(
    title="AnnaBellaDB",
    xaxis_title="Time when access happened",
    yaxis_title="GET access time [sec]",
    font=dict(
        family="Courier New, monospace",
        size=18,
        color="#7f7f7f"
    )
)

now = datetime.now()
fig.write_html("annaBellaDB_{}.html".format(now))
##############################################################################xxx

print("\nPLOT 'annaBellaDB_{}.html' IS SAVED".format(now))

# Stop KVS cluster
print("\nStop KVS cluster...")
execute_bash_command("sudo {0}/scripts/stop-anna-local.sh y".format(LOCAL_ANNA_DIR))

print("DONE")
