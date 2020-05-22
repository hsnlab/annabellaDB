LOCAL_ANNA_DIR = "/home/ubuntu/szalay/anna_original"
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


NF_count_to_run = {'kvs1':1, 'kvs2':2, 'kvs3':3, 'kvs4':4}

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


### Start local KVS (Monitor, Server, Router)
print("Start Master KVS...")

container_name = 'kvs1'

# Delete container if already exist
try:
    cont = client.containers.get(container_name)
    cont.remove(force=True)
except Exception as e:
    print(e)

# start master kvs
execute_bash_command("docker run -it -d --name {} master_annabelladb_image".format(container_name))
time.sleep(1)

# Creating the config file of docker KVS
container = client.containers.get(container_name)
MASTER_IP = container.attrs['NetworkSettings']['IPAddress']

with open('{}/conf/anna-master-template.yml'.format(LOCAL_ANNA_DIR), "rt") as fin:
    with open('{}/conf/anna-{}.yml'.format(LOCAL_ANNA_DIR, container_name), "wt") as fout:
        for line in fin:
            fout.write(line.replace('{DOCKER_IP}', MASTER_IP))

# Copying configuration file to the container
execute_bash_command("docker cp conf/anna-{0}.yml {0}:/hydro/anna/conf/anna-config.yml".format(container_name))

delay = 130
execute_bash_command(
        "docker exec --privileged {} tc qdisc replace dev eth0 root netem delay {}us".format(container_name, delay))

### Start slave KVS
for id in containers:

    container_name = id
    print("\nStart Slave Docker {}...".format(container_name))
    try:
        cont = client.containers.get(container_name)
        cont.remove(force=True)
    except Exception as e:
        print(e)

    # Starting KVS docker container
    execute_bash_command("docker run -it -d --name {} annabelladb_image".format(container_name))
    time.sleep(1)

    # Creating the config file of docker KVS
    container = client.containers.get(container_name)
    docker_ip_add = container.attrs['NetworkSettings']['IPAddress']

    with open('{}/conf/anna-slave-template.yml'.format(LOCAL_ANNA_DIR), "rt") as fin:
        with open('{}/conf/anna-{}.yml'.format(LOCAL_ANNA_DIR, container_name), "wt") as fout:
            for line in fin:
                line = line.replace('{DOCKER_IP}', docker_ip_add)
                line = line.replace('{MASTER_DOCKER_IP}', MASTER_IP)
                fout.write(line)

    execute_bash_command("docker cp conf/anna-{0}.yml {0}:/hydro/anna/conf/anna-config.yml".format(container_name))

    # Set delay to the container's interface
    #delay = (int(container_name[-1:]) - 1) * 5
    delay = 130
    execute_bash_command(
        "docker exec --privileged {} tc qdisc replace dev eth0 root netem delay {}us".format(container_name,
                                                                                             delay))
    # Start KVS
    # execute_bash_command('docker exec -i -d {} /hydro/anna/scripts/start-anna-docker.sh n n&'.format(container_name))
    print("\tTo get {} client:".format(container_name))
    print('\t\tdocker exec -it {} bash -c "/hydro/anna/build/cli/anna-cli hydro/anna/conf/anna-config.yml"'.format(
        container_name))

print("\nWaiting for cluster initialization...")
time.sleep(20)

#######################################################################################################################

ctr = 0
for c in all_containers:
    container_name = c
    for i in range(NF_count_to_run[c]):
        # Start container
        print("\nStart NF{} on docker container {} ...".format(ctr, container_name))
        container = client.containers.get(container_name)
        docker_ip_add = container.attrs['NetworkSettings']['IPAddress']

        kvs_container = client.containers.get(container_name)
        print("\t/KVS-CLIENT/bin/python3 /hydro/anna/client/python/test3_client.py {} a {}".format(docker_ip_add, ctr))
        kvs_container.exec_run(
            "/KVS-CLIENT/bin/python3 /hydro/anna/client/python/test3_client.py {} a {}".format(docker_ip_add, ctr), stderr=True,
            stdout=True, detach=True)
        ctr += 1
   
    time.sleep(60)

    # Stop container
    print("\nStop NFs on {}...".format(c))
    print("\tdocker cp {0}/client/python/test3_state_stop.txt {1}:/hydro/anna/client/python/test3_state.txt".format(LOCAL_ANNA_DIR, c))
    execute_bash_command("docker cp {0}/client/python/test3_state_stop.txt {1}:/hydro/anna/client/python/test3_state.txt".format(LOCAL_ANNA_DIR, c))


time.sleep(5)
for id in all_containers:
    print("\nCollect access data of NFs on {}...".format(id))
    
    for i in range(ctr):
        print("\tdocker cp {0}:/access_time_data_{2}.json {1}/access_pattern_results/{0}_access_pattern_{2}.json".format(
            id, LOCAL_ANNA_DIR, i))
        execute_bash_command(
            "docker cp {0}:/access_time_data_{2}.json {1}/access_pattern_results/{0}_access_pattern_{2}.json".format(
            id, LOCAL_ANNA_DIR, i))

# Collect data
##################################################################################################################

print("\nLoading Access time files...")

import pandas as pd

df = pd.DataFrame(columns=['client', 'access_time', 'access_duration'])

# last_time = 0
for i in range(0,ctr):
    for kvs in all_containers:
        print("\t{}...".format(kvs))
        try:
            with open('access_pattern_results/{}_access_pattern_{}.json'.format(kvs, i)) as json_file:
                localdata = json.load(json_file)

            start_time = None

            for tuple in localdata:
                
                df = df.append({'client': kvs, "access_time": tuple[0], "access_duration": tuple[1], "access_type": tuple[2], "NF":i}, ignore_index=True)
            # last_time = localdata[-1][0]-start_time+1
        except Exception as e:
            print(e)

print(df.head())
print(df.tail())

# scatter(x=list(df.access_time), y=list(df.access_duration))
# fig.show()

fig = go.Figure()

for i in range(0, ctr):
    df_filtered = df[df.NF.eq(i)]
    print(i)
    print(df_filtered.head())
    fig.add_scatter(x=df_filtered.access_time, y=df_filtered.access_duration, name="NF"+str(i), mode='markers')

fig.update_layout(
    title="AnnaBellaDB",
    xaxis_title="Time when access happened",
    yaxis_title="access time [sec]",
    font=dict(
        family="Courier New, monospace",
        size=18,
        color="#7f7f7f"
    )
)

now = datetime.now()
fig.write_html("test3_annaBellaDB_{}.html".format(now))
print("\nPLOT 'test3_annaBellaDB_{}.html' IS SAVED".format(now))

##############################################################################xxx



#ax = sns.scatterplot(x="access_time", y="access_duration", hue="client", data=df)
# plt.show()

# Stop KVS cluster
print("\nStop KVS cluster...")
execute_bash_command("sudo {0}/scripts/stop-anna-local.sh y".format(LOCAL_ANNA_DIR))

print("DONE")
