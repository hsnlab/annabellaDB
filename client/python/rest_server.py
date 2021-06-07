import requests
from flask import Flask, redirect, url_for, request, abort, jsonify
from os import path
import yaml
import json
import anna.client as client
import anna.lattices as lattices
import time
import argparse


### Main function ######################################################################################################

parser = argparse.ArgumentParser(description='Client Wrapper for AnnaBellaDB client.')
parser.add_argument('ip', type=str, help='IP address of the client.')
args = parser.parse_args()

kvs_client = client.AnnaTcpClient(args.ip, args.ip, local=True)

# Starting REST API server
app = Flask(__name__)


#### Error handlers ####################################################################################################
@app.errorhandler(400)
def resource_not_found(e):
    return jsonify(error=str(e)), 400


@app.errorhandler(409)
def resource_not_found(e):
    return jsonify(error=str(e)), 409


### API endpoints ######################################################################################################
@app.route('/')
def hello_world():
    return 'Hello, World!'


@app.route('/get', methods=['POST'])
def get():
    json_data = request.get_json()
    key = json_data['key']
    try:
        values = kvs_client.get(key)
        value = str(values[key])
        print(value)

    except UnboundLocalError as e:
        timeout = "TIMEOUT"
        abort(400, description="TIMEOUT of AnnaBellaDB GET\n".format(e))

    return json.dumps(value)

@app.route('/put', methods=['POST'])
def put():
    json_data = request.get_json()
    key = json_data['key']
    value = json_data['value']

    timestamp = int(time.time() * 1000000)
    value_b = value.encode()
    value = lattices.LWWPairLattice(timestamp, value_b)
    try:
        kvs_client.put(key, value)
    except UnboundLocalError as e:
        timeout = "TIMEOUT"
        abort(400, description="TIMEOUT of AnnaBellaDB PUT\n".format(e))

    return json.dumps("OK")

app.run(host='0.0.0.0')
