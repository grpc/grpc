#!/usr/bin/env python
# Copyright 2020 gRPC authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
"""Run xDS integration tests on GCP using Traffic Director."""

import argparse
import googleapiclient.discovery
import grpc
import logging
import os
import random
import shlex
import socket
import subprocess
import sys
import tempfile
import time

from oauth2client.client import GoogleCredentials

from src.proto.grpc.testing import messages_pb2
from src.proto.grpc.testing import test_pb2_grpc

logger = logging.getLogger()
console_handler = logging.StreamHandler()
logger.addHandler(console_handler)


def parse_port_range(port_arg):
    try:
        port = int(port_arg)
        return range(port, port + 1)
    except:
        port_min, port_max = port_arg.split(':')
        return range(int(port_min), int(port_max) + 1)


argp = argparse.ArgumentParser(description='Run xDS interop tests on GCP')
argp.add_argument('--project_id', help='GCP project id')
argp.add_argument(
    '--gcp_suffix',
    default='',
    help='Optional suffix for all generated GCP resource names. Useful to ensure '
    'distinct names across test runs.')
argp.add_argument('--test_case',
                  default=None,
                  choices=['all', 'ping_pong', 'round_robin'])
argp.add_argument(
    '--client_cmd',
    default=None,
    help='Command to launch xDS test client. This script will fill in '
    '{service_host}, {service_port},{stats_port} and {qps} parameters using '
    'str.format(), and generate the GRPC_XDS_BOOTSTRAP file.')
argp.add_argument('--zone', default='us-central1-a')
argp.add_argument('--qps', default=10, help='Client QPS')
argp.add_argument(
    '--wait_for_backend_sec',
    default=900,
    help='Time limit for waiting for created backend services to report healthy '
    'when launching test suite')
argp.add_argument(
    '--keep_gcp_resources',
    default=False,
    action='store_true',
    help=
    'Leave GCP VMs and configuration running after test. Default behavior is '
    'to delete when tests complete.')
argp.add_argument(
    '--compute_discovery_document',
    default=None,
    type=str,
    help=
    'If provided, uses this file instead of retrieving via the GCP discovery API'
)
argp.add_argument('--network',
                  default='global/networks/default',
                  help='GCP network to use')
argp.add_argument('--service_port_range',
                  default='8080:8180',
                  type=parse_port_range,
                  help='Listening port for created gRPC backends. Specified as '
                  'either a single int or as a range in the format min:max, in '
                  'which case an available port p will be chosen s.t. min <= p '
                  '<= max')
argp.add_argument(
    '--stats_port',
    default=8079,
    type=int,
    help='Local port for the client process to expose the LB stats service')
argp.add_argument('--xds_server',
                  default='trafficdirector.googleapis.com:443',
                  help='xDS server')
argp.add_argument('--source_image',
                  default='projects/debian-cloud/global/images/family/debian-9',
                  help='Source image for VMs created during the test')
argp.add_argument(
    '--tolerate_gcp_errors',
    default=False,
    action='store_true',
    help=
    'Continue with test even when an error occurs during setup. Intended for '
    'manual testing, where attempts to recreate any GCP resources already '
    'existing will result in an error')
argp.add_argument('--verbose',
                  help='verbose log output',
                  default=False,
                  action="store_true")
args = argp.parse_args()

if args.verbose:
    logger.setLevel(logging.DEBUG)

PROJECT_ID = args.project_id
ZONE = args.zone
QPS = args.qps
TEST_CASE = args.test_case
CLIENT_CMD = args.client_cmd
WAIT_FOR_BACKEND_SEC = args.wait_for_backend_sec
TEMPLATE_NAME = 'test-template' + args.gcp_suffix
INSTANCE_GROUP_NAME = 'test-ig' + args.gcp_suffix
HEALTH_CHECK_NAME = 'test-hc' + args.gcp_suffix
FIREWALL_RULE_NAME = 'test-fw-rule' + args.gcp_suffix
BACKEND_SERVICE_NAME = 'test-backend-service' + args.gcp_suffix
URL_MAP_NAME = 'test-map' + args.gcp_suffix
SERVICE_HOST = 'grpc-test' + args.gcp_suffix
TARGET_PROXY_NAME = 'test-target-proxy' + args.gcp_suffix
FORWARDING_RULE_NAME = 'test-forwarding-rule' + args.gcp_suffix
KEEP_GCP_RESOURCES = args.keep_gcp_resources
TOLERATE_GCP_ERRORS = args.tolerate_gcp_errors
STATS_PORT = args.stats_port
INSTANCE_GROUP_SIZE = 2
WAIT_FOR_OPERATION_SEC = 60
NUM_TEST_RPCS = 10 * QPS
WAIT_FOR_STATS_SEC = 30
BOOTSTRAP_TEMPLATE = """
{{
  "node": {{
    "id": "{node_id}"
  }},
  "xds_servers": [{{
    "server_uri": "%s",
    "channel_creds": [
      {{
        "type": "google_default",
        "config": {{}}
      }}
    ]
  }}]
}}""" % args.xds_server


def get_client_stats(num_rpcs, timeout_sec):
    with grpc.insecure_channel('localhost:%d' % STATS_PORT) as channel:
        stub = test_pb2_grpc.LoadBalancerStatsServiceStub(channel)
        request = messages_pb2.LoadBalancerStatsRequest()
        request.num_rpcs = num_rpcs
        request.timeout_sec = timeout_sec
        rpc_timeout = timeout_sec * 2  # Allow time for connection establishment
        try:
            response = stub.GetClientStats(request,
                                           wait_for_ready=True,
                                           timeout=rpc_timeout)
            logger.debug('Invoked GetClientStats RPC: %s', response)
            return response
        except grpc.RpcError as rpc_error:
            raise Exception('GetClientStats RPC failed')


def wait_until_only_given_backends_receive_load(backends, timeout_sec):
    start_time = time.time()
    error_msg = None
    while time.time() - start_time <= timeout_sec:
        error_msg = None
        stats = get_client_stats(max(len(backends), 1), timeout_sec)
        rpcs_by_peer = stats.rpcs_by_peer
        for backend in backends:
            if backend not in rpcs_by_peer:
                error_msg = 'Backend %s did not receive load' % backend
                break
        if not error_msg and len(rpcs_by_peer) > len(backends):
            error_msg = 'Unexpected backend received load: %s' % rpcs_by_peer
        if not error_msg:
            return
    raise Exception(error_msg)


def test_ping_pong(backends, num_rpcs, stats_timeout_sec):
    start_time = time.time()
    error_msg = None
    while time.time() - start_time <= stats_timeout_sec:
        error_msg = None
        stats = get_client_stats(num_rpcs, stats_timeout_sec)
        rpcs_by_peer = stats.rpcs_by_peer
        for backend in backends:
            if backend not in rpcs_by_peer:
                error_msg = 'Backend %s did not receive load' % backend
                break
        if not error_msg and len(rpcs_by_peer) > len(backends):
            error_msg = 'Unexpected backend received load: %s' % rpcs_by_peer
        if not error_msg:
            return
    raise Exception(error_msg)


def test_round_robin(backends, num_rpcs, stats_timeout_sec):
    threshold = 1
    wait_until_only_given_backends_receive_load(backends, stats_timeout_sec)
    stats = get_client_stats(num_rpcs, stats_timeout_sec)
    requests_received = [stats.rpcs_by_peer[x] for x in stats.rpcs_by_peer]
    total_requests_received = sum(
        [stats.rpcs_by_peer[x] for x in stats.rpcs_by_peer])
    if total_requests_received != num_rpcs:
        raise Exception('Unexpected RPC failures', stats)
    expected_requests = total_requests_received / len(backends)
    for backend in backends:
        if abs(stats.rpcs_by_peer[backend] - expected_requests) > threshold:
            raise Exception(
                'RPC peer distribution differs from expected by more than %d for backend %s (%s)',
                threshold, backend, stats)


def create_instance_template(compute, project, name, grpc_port):
    config = {
        'name': name,
        'properties': {
            'tags': {
                'items': ['grpc-allow-healthcheck']
            },
            'machineType': 'e2-standard-2',
            'serviceAccounts': [{
                'email': 'default',
                'scopes': ['https://www.googleapis.com/auth/cloud-platform',]
            }],
            'networkInterfaces': [{
                'accessConfigs': [{
                    'type': 'ONE_TO_ONE_NAT'
                }],
                'network': args.network
            }],
            'disks': [{
                'boot': True,
                'initializeParams': {
                    'sourceImage': args.source_image
                }
            }],
            'metadata': {
                'items': [{
                    'key':
                        'startup-script',
                    'value':
                        """#!/bin/bash

sudo apt update
sudo apt install -y git default-jdk
mkdir java_server
pushd java_server
git clone https://github.com/grpc/grpc-java.git
pushd grpc-java
pushd interop-testing
../gradlew installDist -x test -PskipCodegen=true -PskipAndroid=true

nohup build/install/grpc-interop-testing/bin/xds-test-server --port=%d 1>/dev/null &"""
                        % grpc_port
                }]
            }
        }
    }

    result = compute.instanceTemplates().insert(project=project,
                                                body=config).execute()
    wait_for_global_operation(compute, project, result['name'])
    return result['targetLink']


def create_instance_group(compute, project, zone, name, size, grpc_port,
                          template_url):
    config = {
        'name': name,
        'instanceTemplate': template_url,
        'targetSize': size,
        'namedPorts': [{
            'name': 'grpc',
            'port': grpc_port
        }]
    }

    result = compute.instanceGroupManagers().insert(project=project,
                                                    zone=zone,
                                                    body=config).execute()
    wait_for_zone_operation(compute, project, zone, result['name'])
    result = compute.instanceGroupManagers().get(
        project=PROJECT_ID, zone=ZONE, instanceGroupManager=name).execute()
    return result['instanceGroup']


def create_health_check(compute, project, name):
    config = {
        'name': name,
        'type': 'TCP',
        'tcpHealthCheck': {
            'portName': 'grpc'
        }
    }
    result = compute.healthChecks().insert(project=project,
                                           body=config).execute()
    wait_for_global_operation(compute, project, result['name'])
    return result['targetLink']


def create_health_check_firewall_rule(compute, project, name):
    config = {
        'name': name,
        'direction': 'INGRESS',
        'allowed': [{
            'IPProtocol': 'tcp'
        }],
        'sourceRanges': ['35.191.0.0/16', '130.211.0.0/22'],
        'targetTags': ['grpc-allow-healthcheck'],
    }
    result = compute.firewalls().insert(project=project, body=config).execute()
    wait_for_global_operation(compute, project, result['name'])


def create_backend_service(compute, project, name, health_check):
    config = {
        'name': name,
        'loadBalancingScheme': 'INTERNAL_SELF_MANAGED',
        'healthChecks': [health_check],
        'portName': 'grpc',
        'protocol': 'HTTP2'
    }
    result = compute.backendServices().insert(project=project,
                                              body=config).execute()
    wait_for_global_operation(compute, project, result['name'])
    return result['targetLink']


def create_url_map(compute, project, name, backend_service_url, host_name):
    path_matcher_name = 'path-matcher'
    config = {
        'name': name,
        'defaultService': backend_service_url,
        'pathMatchers': [{
            'name': path_matcher_name,
            'defaultService': backend_service_url,
        }],
        'hostRules': [{
            'hosts': [host_name],
            'pathMatcher': path_matcher_name
        }]
    }
    result = compute.urlMaps().insert(project=project, body=config).execute()
    wait_for_global_operation(compute, project, result['name'])
    return result['targetLink']


def create_target_http_proxy(compute, project, name, url_map_url):
    config = {
        'name': name,
        'url_map': url_map_url,
    }
    result = compute.targetHttpProxies().insert(project=project,
                                                body=config).execute()
    wait_for_global_operation(compute, project, result['name'])
    return result['targetLink']


def create_global_forwarding_rule(compute, project, name, grpc_port,
                                  target_http_proxy_url):
    config = {
        'name': name,
        'loadBalancingScheme': 'INTERNAL_SELF_MANAGED',
        'portRange': str(grpc_port),
        'IPAddress': '0.0.0.0',
        'network': args.network,
        'target': target_http_proxy_url,
    }
    result = compute.globalForwardingRules().insert(project=project,
                                                    body=config).execute()
    wait_for_global_operation(compute, project, result['name'])


def delete_global_forwarding_rule(compute, project, forwarding_rule):
    try:
        result = compute.globalForwardingRules().delete(
            project=project, forwardingRule=forwarding_rule).execute()
        wait_for_global_operation(compute, project, result['name'])
    except googleapiclient.errors.HttpError as http_error:
        logger.info('Delete failed: %s', http_error)


def delete_target_http_proxy(compute, project, target_http_proxy):
    try:
        result = compute.targetHttpProxies().delete(
            project=project, targetHttpProxy=target_http_proxy).execute()
        wait_for_global_operation(compute, project, result['name'])
    except googleapiclient.errors.HttpError as http_error:
        logger.info('Delete failed: %s', http_error)


def delete_url_map(compute, project, url_map):
    try:
        result = compute.urlMaps().delete(project=project,
                                          urlMap=url_map).execute()
        wait_for_global_operation(compute, project, result['name'])
    except googleapiclient.errors.HttpError as http_error:
        logger.info('Delete failed: %s', http_error)


def delete_backend_service(compute, project, backend_service):
    try:
        result = compute.backendServices().delete(
            project=project, backendService=backend_service).execute()
        wait_for_global_operation(compute, project, result['name'])
    except googleapiclient.errors.HttpError as http_error:
        logger.info('Delete failed: %s', http_error)


def delete_firewall(compute, project, firewall_rule):
    try:
        result = compute.firewalls().delete(project=project,
                                            firewall=firewall_rule).execute()
        wait_for_global_operation(compute, project, result['name'])
    except googleapiclient.errors.HttpError as http_error:
        logger.info('Delete failed: %s', http_error)


def delete_health_check(compute, project, health_check):
    try:
        result = compute.healthChecks().delete(
            project=project, healthCheck=health_check).execute()
        wait_for_global_operation(compute, project, result['name'])
    except googleapiclient.errors.HttpError as http_error:
        logger.info('Delete failed: %s', http_error)


def delete_instance_group(compute, project, zone, instance_group):
    try:
        result = compute.instanceGroupManagers().delete(
            project=project, zone=zone,
            instanceGroupManager=instance_group).execute()
        timeout_sec = 180  # Deleting an instance group can be slow
        wait_for_zone_operation(compute,
                                project,
                                ZONE,
                                result['name'],
                                timeout_sec=timeout_sec)
    except googleapiclient.errors.HttpError as http_error:
        logger.info('Delete failed: %s', http_error)


def delete_instance_template(compute, project, instance_template):
    try:
        result = compute.instanceTemplates().delete(
            project=project, instanceTemplate=instance_template).execute()
        wait_for_global_operation(compute, project, result['name'])
    except googleapiclient.errors.HttpError as http_error:
        logger.info('Delete failed: %s', http_error)


def add_instances_to_backend(compute, project, backend_service, instance_group):
    config = {
        'backends': [{
            'group': instance_group,
        }],
    }
    result = compute.backendServices().patch(project=project,
                                             backendService=backend_service,
                                             body=config).execute()
    wait_for_global_operation(compute, project, result['name'])


def wait_for_global_operation(compute,
                              project,
                              operation,
                              timeout_sec=WAIT_FOR_OPERATION_SEC):
    start_time = time.time()
    while time.time() - start_time <= timeout_sec:
        result = compute.globalOperations().get(project=project,
                                                operation=operation).execute()
        if result['status'] == 'DONE':
            if 'error' in result:
                raise Exception(result['error'])
            return
        time.sleep(1)
    raise Exception('Operation %s did not complete within %d', operation,
                    timeout_sec)


def wait_for_zone_operation(compute,
                            project,
                            zone,
                            operation,
                            timeout_sec=WAIT_FOR_OPERATION_SEC):
    start_time = time.time()
    while time.time() - start_time <= timeout_sec:
        result = compute.zoneOperations().get(project=project,
                                              zone=zone,
                                              operation=operation).execute()
        if result['status'] == 'DONE':
            if 'error' in result:
                raise Exception(result['error'])
            return
        time.sleep(1)
    raise Exception('Operation %s did not complete within %d', operation,
                    timeout_sec)


def wait_for_healthy_backends(compute, project_id, backend_service,
                              instance_group_url, timeout_sec):
    start_time = time.time()
    config = {'group': instance_group_url}
    while time.time() - start_time <= timeout_sec:
        result = compute.backendServices().getHealth(
            project=project_id, backendService=backend_service,
            body=config).execute()
        if 'healthStatus' in result:
            healthy = True
            for instance in result['healthStatus']:
                if instance['healthState'] != 'HEALTHY':
                    healthy = False
                    break
            if healthy:
                return
        time.sleep(1)
    raise Exception('Not all backends became healthy within %d seconds: %s' %
                    (timeout_sec, result))


def start_xds_client(service_port):
    cmd = CLIENT_CMD.format(service_host=SERVICE_HOST,
                            service_port=service_port,
                            stats_port=STATS_PORT,
                            qps=QPS)
    bootstrap_path = None
    with tempfile.NamedTemporaryFile(delete=False) as bootstrap_file:
        bootstrap_file.write(
            BOOTSTRAP_TEMPLATE.format(
                node_id=socket.gethostname()).encode('utf-8'))
        bootstrap_path = bootstrap_file.name

    client_process = subprocess.Popen(shlex.split(cmd),
                                      env=dict(
                                          os.environ,
                                          GRPC_XDS_BOOTSTRAP=bootstrap_path))
    return client_process


if args.compute_discovery_document:
    with open(args.compute_discovery_document, 'r') as discovery_doc:
        compute = googleapiclient.discovery.build_from_document(
            discovery_doc.read())
else:
    compute = googleapiclient.discovery.build('compute', 'v1')

service_port = None
client_process = None

try:
    instance_group_url = None
    try:
        health_check_url = create_health_check(compute, PROJECT_ID,
                                               HEALTH_CHECK_NAME)
        create_health_check_firewall_rule(compute, PROJECT_ID,
                                          FIREWALL_RULE_NAME)
        backend_service_url = create_backend_service(compute, PROJECT_ID,
                                                     BACKEND_SERVICE_NAME,
                                                     health_check_url)
        url_map_url = create_url_map(compute, PROJECT_ID, URL_MAP_NAME,
                                     backend_service_url, SERVICE_HOST)
        target_http_proxy_url = create_target_http_proxy(
            compute, PROJECT_ID, TARGET_PROXY_NAME, url_map_url)
        potential_service_ports = list(args.service_port_range)
        random.shuffle(potential_service_ports)
        for port in potential_service_ports:
            try:
                create_global_forwarding_rule(
                    compute,
                    PROJECT_ID,
                    FORWARDING_RULE_NAME,
                    port,
                    target_http_proxy_url,
                )
                service_port = port
                break
            except googleapiclient.errors.HttpError as http_error:
                logger.warning(
                    'Got error %s when attempting to create forwarding rule to port %d. Retrying with another port.'
                    % (http_error, port))
        if not service_port:
            raise Exception('Failed to pick a service port in the range %s' %
                            args.service_port_range)
        template_url = create_instance_template(compute, PROJECT_ID,
                                                TEMPLATE_NAME, service_port)
        instance_group_url = create_instance_group(compute, PROJECT_ID, ZONE,
                                                   INSTANCE_GROUP_NAME,
                                                   INSTANCE_GROUP_SIZE,
                                                   service_port, template_url)
        add_instances_to_backend(compute, PROJECT_ID, BACKEND_SERVICE_NAME,
                                 instance_group_url)
    except googleapiclient.errors.HttpError as http_error:
        if TOLERATE_GCP_ERRORS:
            logger.warning(
                'Failed to set up backends: %s. Continuing since '
                '--tolerate_gcp_errors=true', http_error)
        else:
            raise http_error

    if instance_group_url is None:
        # Look up the instance group URL, which may be unset if we are running
        # with --tolerate_gcp_errors=true.
        result = compute.instanceGroups().get(
            project=PROJECT_ID, zone=ZONE,
            instanceGroup=INSTANCE_GROUP_NAME).execute()
        instance_group_url = result['selfLink']
    wait_for_healthy_backends(compute, PROJECT_ID, BACKEND_SERVICE_NAME,
                              instance_group_url, WAIT_FOR_BACKEND_SEC)

    backends = []
    result = compute.instanceGroups().listInstances(
        project=PROJECT_ID,
        zone=ZONE,
        instanceGroup=INSTANCE_GROUP_NAME,
        body={
            'instanceState': 'ALL'
        }).execute()
    for item in result['items']:
        # listInstances() returns the full URL of the instance, which ends with
        # the instance name. compute.instances().get() requires using the
        # instance name (not the full URL) to look up instance details, so we
        # just extract the name manually.
        instance_name = item['instance'].split('/')[-1]
        backends.append(instance_name)

    client_process = start_xds_client(service_port)

    if TEST_CASE == 'all':
        test_ping_pong(backends, NUM_TEST_RPCS, WAIT_FOR_STATS_SEC)
        test_round_robin(backends, NUM_TEST_RPCS, WAIT_FOR_STATS_SEC)
    elif TEST_CASE == 'ping_pong':
        test_ping_pong(backends, NUM_TEST_RPCS, WAIT_FOR_STATS_SEC)
    elif TEST_CASE == 'round_robin':
        test_round_robin(backends, NUM_TEST_RPCS, WAIT_FOR_STATS_SEC)
    else:
        logger.error('Unknown test case: %s', TEST_CASE)
        sys.exit(1)
finally:
    if client_process:
        client_process.terminate()
    if not KEEP_GCP_RESOURCES:
        logger.info('Cleaning up GCP resources. This may take some time.')
        delete_global_forwarding_rule(compute, PROJECT_ID, FORWARDING_RULE_NAME)
        delete_target_http_proxy(compute, PROJECT_ID, TARGET_PROXY_NAME)
        delete_url_map(compute, PROJECT_ID, URL_MAP_NAME)
        delete_backend_service(compute, PROJECT_ID, BACKEND_SERVICE_NAME)
        delete_firewall(compute, PROJECT_ID, FIREWALL_RULE_NAME)
        delete_health_check(compute, PROJECT_ID, HEALTH_CHECK_NAME)
        delete_instance_group(compute, PROJECT_ID, ZONE, INSTANCE_GROUP_NAME)
        delete_instance_template(compute, PROJECT_ID, TEMPLATE_NAME)
