#!/usr/bin/env python2.7
# Copyright 2015-2016, Google Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
import argparse
import datetime
import os
import subprocess
import sys
import time

stress_test_utils_dir = os.path.abspath(os.path.join(
    os.path.dirname(__file__), '../../gcp/stress_test'))
sys.path.append(stress_test_utils_dir)
from stress_test_utils import BigQueryHelper

kubernetes_api_dir = os.path.abspath(os.path.join(
    os.path.dirname(__file__), '../../gcp/utils'))
sys.path.append(kubernetes_api_dir)

import kubernetes_api

_GRPC_ROOT = os.path.abspath(os.path.join(
    os.path.dirname(sys.argv[0]), '../../..'))
os.chdir(_GRPC_ROOT)

# num of seconds to wait for the GKE image to start and warmup
_GKE_IMAGE_WARMUP_WAIT_SECS = 60

_SERVER_POD_NAME = 'stress-server'
_CLIENT_POD_NAME_PREFIX = 'stress-client'
_DATASET_ID_PREFIX = 'stress_test'
_SUMMARY_TABLE_ID = 'summary'
_QPS_TABLE_ID = 'qps'

_DEFAULT_DOCKER_IMAGE_NAME = 'grpc_stress_test'

# The default port on which the kubernetes proxy server is started on localhost
# (i.e kubectl proxy --port=<port>)
_DEFAULT_KUBERNETES_PROXY_PORT = 8001

# How frequently should the stress client wrapper script (running inside a GKE
# container) poll the health of the stress client (also running inside the GKE
# container) and upload metrics to BigQuery
_DEFAULT_STRESS_CLIENT_POLL_INTERVAL_SECS = 60

# The default setting for stress test server and client
_DEFAULT_STRESS_SERVER_PORT = 8080
_DEFAULT_METRICS_PORT = 8081
_DEFAULT_TEST_CASES_STR = 'empty_unary:1,large_unary:1,client_streaming:1,server_streaming:1,empty_stream:1'
_DEFAULT_NUM_CHANNELS_PER_SERVER = 5
_DEFAULT_NUM_STUBS_PER_CHANNEL = 10
_DEFAULT_METRICS_COLLECTION_INTERVAL_SECS = 30

# Number of stress client instances to launch
_DEFAULT_NUM_CLIENTS = 3

# How frequently should this test monitor the health of Stress clients and
# Servers running in GKE
_DEFAULT_TEST_POLL_INTERVAL_SECS = 60

# Default run time for this test (2 hour)
_DEFAULT_TEST_DURATION_SECS = 7200

# The number of seconds it would take a GKE pod to warm up (i.e get to 'Running'
# state from the time of creation). Ideally this is something the test should
# automatically determine by using Kubernetes API to poll the pods status.
_DEFAULT_GKE_WARMUP_SECS = 60


class KubernetesProxy:
  """ Class to start a proxy on localhost to the Kubernetes API server """

  def __init__(self, api_port):
    self.port = api_port
    self.p = None
    self.started = False

  def start(self):
    cmd = ['kubectl', 'proxy', '--port=%d' % self.port]
    self.p = subprocess.Popen(args=cmd)
    self.started = True
    time.sleep(2)
    print '..Started'

  def get_port(self):
    return self.port

  def is_started(self):
    return self.started

  def __del__(self):
    if self.p is not None:
      print 'Shutting down Kubernetes proxy..'
      self.p.kill()


class TestSettings:

  def __init__(self, build_docker_image, test_poll_interval_secs,
               test_duration_secs, kubernetes_proxy_port):
    self.build_docker_image = build_docker_image
    self.test_poll_interval_secs = test_poll_interval_secs
    self.test_duration_secs = test_duration_secs
    self.kubernetes_proxy_port = kubernetes_proxy_port


class GkeSettings:

  def __init__(self, project_id, docker_image_name):
    self.project_id = project_id
    self.docker_image_name = docker_image_name
    self.tag_name = 'gcr.io/%s/%s' % (project_id, docker_image_name)


class BigQuerySettings:

  def __init__(self, run_id, dataset_id, summary_table_id, qps_table_id):
    self.run_id = run_id
    self.dataset_id = dataset_id
    self.summary_table_id = summary_table_id
    self.qps_table_id = qps_table_id


class StressServerSettings:

  def __init__(self, server_pod_name, server_port):
    self.server_pod_name = server_pod_name
    self.server_port = server_port


class StressClientSettings:

  def __init__(self, num_clients, client_pod_name_prefix, server_pod_name,
               server_port, metrics_port, metrics_collection_interval_secs,
               stress_client_poll_interval_secs, num_channels_per_server,
               num_stubs_per_channel, test_cases_str):
    self.num_clients = num_clients
    self.client_pod_name_prefix = client_pod_name_prefix
    self.server_pod_name = server_pod_name
    self.server_port = server_port
    self.metrics_port = metrics_port
    self.metrics_collection_interval_secs = metrics_collection_interval_secs
    self.stress_client_poll_interval_secs = stress_client_poll_interval_secs
    self.num_channels_per_server = num_channels_per_server
    self.num_stubs_per_channel = num_stubs_per_channel
    self.test_cases_str = test_cases_str

    # == Derived properties ==
    # Note: Client can accept a list of server addresses (a comma separated list
    # of 'server_name:server_port'). In this case, we only have one server
    # address to pass
    self.server_addresses = '%s.default.svc.cluster.local:%d' % (
        server_pod_name, server_port)
    self.client_pod_names_list = ['%s-%d' % (client_pod_name_prefix, i)
                                  for i in range(1, num_clients + 1)]


def _build_docker_image(image_name, tag_name):
  """ Build the docker image and add tag it to the GKE repository """
  print 'Building docker image: %s' % image_name
  os.environ['INTEROP_IMAGE'] = image_name
  os.environ['INTEROP_IMAGE_REPOSITORY_TAG'] = tag_name
  # Note that 'BASE_NAME' HAS to be 'grpc_interop_stress_cxx' since the script
  # build_interop_stress_image.sh invokes the following script:
  #   tools/dockerfile/$BASE_NAME/build_interop_stress.sh
  os.environ['BASE_NAME'] = 'grpc_interop_stress_cxx'
  cmd = ['tools/jenkins/build_interop_stress_image.sh']
  retcode = subprocess.call(args=cmd)
  if retcode != 0:
    print 'Error in building docker image'
    return False
  return True


def _push_docker_image_to_gke_registry(docker_tag_name):
  """Executes 'gcloud docker push <docker_tag_name>' to push the image to GKE registry"""
  cmd = ['gcloud', 'docker', 'push', docker_tag_name]
  print 'Pushing %s to GKE registry..' % docker_tag_name
  retcode = subprocess.call(args=cmd)
  if retcode != 0:
    print 'Error in pushing docker image %s to the GKE registry' % docker_tag_name
    return False
  return True


def _launch_server(gke_settings, stress_server_settings, bq_settings,
                   kubernetes_proxy):
  """ Launches a stress test server instance in GKE cluster """
  if not kubernetes_proxy.is_started:
    print 'Kubernetes proxy must be started before calling this function'
    return False

  # This is the wrapper script that is run in the container. This script runs
  # the actual stress test server
  server_cmd_list = ['/var/local/git/grpc/tools/gcp/stress_test/run_server.py']

  # run_server.py does not take any args from the command line. The args are
  # instead passed via environment variables (see server_env below)
  server_arg_list = []

  # The parameters to the script run_server.py are injected into the container
  # via environment variables
  server_env = {
      'STRESS_TEST_IMAGE_TYPE': 'SERVER',
      'STRESS_TEST_IMAGE': '/var/local/git/grpc/bins/opt/interop_server',
      'STRESS_TEST_ARGS_STR': '--port=%s' % stress_server_settings.server_port,
      'RUN_ID': bq_settings.run_id,
      'POD_NAME': stress_server_settings.server_pod_name,
      'GCP_PROJECT_ID': gke_settings.project_id,
      'DATASET_ID': bq_settings.dataset_id,
      'SUMMARY_TABLE_ID': bq_settings.summary_table_id,
      'QPS_TABLE_ID': bq_settings.qps_table_id
  }

  # Launch Server
  is_success = kubernetes_api.create_pod_and_service(
      'localhost',
      kubernetes_proxy.get_port(),
      'default',  # Use 'default' namespace
      stress_server_settings.server_pod_name,
      gke_settings.tag_name,
      [stress_server_settings.server_port],  # Port that should be exposed
      server_cmd_list,
      server_arg_list,
      server_env,
      True  # Headless = True for server. Since we want DNS records to be created by GKE
  )

  return is_success


def _launch_client(gke_settings, stress_server_settings, stress_client_settings,
                   bq_settings, kubernetes_proxy):
  """ Launches a configurable number of stress test clients on GKE cluster """
  if not kubernetes_proxy.is_started:
    print 'Kubernetes proxy must be started before calling this function'
    return False

  stress_client_arg_list = [
      '--server_addresses=%s' % stress_client_settings.server_addresses,
      '--test_cases=%s' % stress_client_settings.test_cases_str,
      '--num_stubs_per_channel=%d' %
      stress_client_settings.num_stubs_per_channel
  ]

  # This is the wrapper script that is run in the container. This script runs
  # the actual stress client
  client_cmd_list = ['/var/local/git/grpc/tools/gcp/stress_test/run_client.py']

  # run_client.py takes no args. All args are passed as env variables (see
  # client_env)
  client_arg_list = []

  metrics_server_address = 'localhost:%d' % stress_client_settings.metrics_port
  metrics_client_arg_list = [
      '--metrics_server_address=%s' % metrics_server_address,
      '--total_only=true'
  ]

  # The parameters to the script run_client.py are injected into the container
  # via environment variables
  client_env = {
      'STRESS_TEST_IMAGE_TYPE': 'CLIENT',
      'STRESS_TEST_IMAGE': '/var/local/git/grpc/bins/opt/stress_test',
      'STRESS_TEST_ARGS_STR': ' '.join(stress_client_arg_list),
      'METRICS_CLIENT_IMAGE': '/var/local/git/grpc/bins/opt/metrics_client',
      'METRICS_CLIENT_ARGS_STR': ' '.join(metrics_client_arg_list),
      'RUN_ID': bq_settings.run_id,
      'POLL_INTERVAL_SECS':
          str(stress_client_settings.stress_client_poll_interval_secs),
      'GCP_PROJECT_ID': gke_settings.project_id,
      'DATASET_ID': bq_settings.dataset_id,
      'SUMMARY_TABLE_ID': bq_settings.summary_table_id,
      'QPS_TABLE_ID': bq_settings.qps_table_id
  }

  for pod_name in stress_client_settings.client_pod_names_list:
    client_env['POD_NAME'] = pod_name
    is_success = kubernetes_api.create_pod_and_service(
        'localhost',  # Since proxy is running on localhost
        kubernetes_proxy.get_port(),
        'default',  # default namespace
        pod_name,
        gke_settings.tag_name,
        [stress_client_settings.metrics_port
        ],  # Client pods expose metrics port
        client_cmd_list,
        client_arg_list,
        client_env,
        False  # Client is not a headless service
    )
    if not is_success:
      print 'Error in launching client %s' % pod_name
      return False

  return True


def _launch_server_and_client(gke_settings, stress_server_settings,
                              stress_client_settings, bq_settings,
                              kubernetes_proxy_port):
  # Start kubernetes proxy
  print 'Kubernetes proxy'
  kubernetes_proxy = KubernetesProxy(kubernetes_proxy_port)
  kubernetes_proxy.start()

  print 'Launching server..'
  is_success = _launch_server(gke_settings, stress_server_settings, bq_settings,
                              kubernetes_proxy)
  if not is_success:
    print 'Error in launching server'
    return False

  # Server takes a while to start.
  # TODO(sree) Use Kubernetes API to query the status of the server instead of
  # sleeping
  print 'Waiting for %s seconds for the server to start...' % _GKE_IMAGE_WARMUP_WAIT_SECS
  time.sleep(_GKE_IMAGE_WARMUP_WAIT_SECS)

  # Launch client
  client_pod_name_prefix = 'stress-client'
  is_success = _launch_client(gke_settings, stress_server_settings,
                              stress_client_settings, bq_settings,
                              kubernetes_proxy)

  if not is_success:
    print 'Error in launching client(s)'
    return False

  print 'Waiting for %s seconds for the client images to start...' % _GKE_IMAGE_WARMUP_WAIT_SECS
  time.sleep(_GKE_IMAGE_WARMUP_WAIT_SECS)
  return True


def _delete_server_and_client(stress_server_settings, stress_client_settings,
                              kubernetes_proxy_port):
  kubernetes_proxy = KubernetesProxy(kubernetes_proxy_port)
  kubernetes_proxy.start()

  # Delete clients first
  is_success = True
  for pod_name in stress_client_settings.client_pod_names_list:
    is_success = kubernetes_api.delete_pod_and_service(
        'localhost', kubernetes_proxy_port, 'default', pod_name)
    if not is_success:
      return False

  # Delete server
  is_success = kubernetes_api.delete_pod_and_service(
      'localhost', kubernetes_proxy_port, 'default',
      stress_server_settings.server_pod_name)
  return is_success


def run_test_main(test_settings, gke_settings, stress_server_settings,
                  stress_client_clients):
  is_success = True

  if test_settings.build_docker_image:
    is_success = _build_docker_image(gke_settings.docker_image_name,
                                     gke_settings.tag_name)
    if not is_success:
      return False

    is_success = _push_docker_image_to_gke_registry(gke_settings.tag_name)
    if not is_success:
      return False

  # Create a unique id for this run (Note: Using timestamp instead of UUID to
  # make it easier to deduce the date/time of the run just by looking at the run
  # run id. This is useful in debugging when looking at records in Biq query)
  run_id = datetime.datetime.now().strftime('%Y_%m_%d_%H_%M_%S')
  dataset_id = '%s_%s' % (_DATASET_ID_PREFIX, run_id)

  # Big Query settings (common for both Stress Server and Client)
  bq_settings = BigQuerySettings(run_id, dataset_id, _SUMMARY_TABLE_ID,
                                 _QPS_TABLE_ID)

  bq_helper = BigQueryHelper(run_id, '', '', args.project_id, dataset_id,
                             _SUMMARY_TABLE_ID, _QPS_TABLE_ID)
  bq_helper.initialize()

  try:
    is_success = _launch_server_and_client(gke_settings, stress_server_settings,
                                           stress_client_settings, bq_settings,
                                           test_settings.kubernetes_proxy_port)
    if not is_success:
      return False

    start_time = datetime.datetime.now()
    end_time = start_time + datetime.timedelta(
        seconds=test_settings.test_duration_secs)
    print 'Running the test until %s' % end_time.isoformat()

    while True:
      if datetime.datetime.now() > end_time:
        print 'Test was run for %d seconds' % test_settings.test_duration_secs
        break

      # Check if either stress server or clients have failed
      if bq_helper.check_if_any_tests_failed():
        is_success = False
        print 'Some tests failed.'
        break

      # Things seem to be running fine. Wait until next poll time to check the
      # status
      print 'Sleeping for %d seconds..' % test_settings.test_poll_interval_secs
      time.sleep(test_settings.test_poll_interval_secs)

    # Print BiqQuery tables
    bq_helper.print_summary_records()
    bq_helper.print_qps_records()

  finally:
    # If is_success is False at this point, it means that the stress tests were
    # started successfully but failed while running the tests. In this case we
    # do should not delete the pods (since they contain all the failure
    # information)
    if is_success:
      _delete_server_and_client(stress_server_settings, stress_client_settings,
                                test_settings.kubernetes_proxy_port)

  return is_success


argp = argparse.ArgumentParser(
    description='Launch stress tests in GKE',
    formatter_class=argparse.ArgumentDefaultsHelpFormatter)
argp.add_argument('--project_id',
                  required=True,
                  help='The Google Cloud Platform Project Id')
argp.add_argument('--num_clients',
                  default=1,
                  type=int,
                  help='Number of client instances to start')
argp.add_argument('--docker_image_name',
                  default=_DEFAULT_DOCKER_IMAGE_NAME,
                  help='The name of the docker image containing stress client '
                  'and stress servers')
argp.add_argument('--build_docker_image',
                  dest='build_docker_image',
                  action='store_true',
                  help='Build a docker image and push to Google Container '
                  'Registry')
argp.add_argument('--do_not_build_docker_image',
                  dest='build_docker_image',
                  action='store_false',
                  help='Do not build and push docker image to Google Container '
                  'Registry')
argp.set_defaults(build_docker_image=True)

argp.add_argument('--test_poll_interval_secs',
                  default=_DEFAULT_TEST_POLL_INTERVAL_SECS,
                  type=int,
                  help='How frequently should this script should monitor the '
                  'health of stress clients and servers running in the GKE '
                  'cluster')
argp.add_argument('--test_duration_secs',
                  default=_DEFAULT_TEST_DURATION_SECS,
                  type=int,
                  help='How long should this test be run')
argp.add_argument('--kubernetes_proxy_port',
                  default=_DEFAULT_KUBERNETES_PROXY_PORT,
                  type=int,
                  help='The port on which the kubernetes proxy (on localhost)'
                  ' is started')
argp.add_argument('--stress_server_port',
                  default=_DEFAULT_STRESS_SERVER_PORT,
                  type=int,
                  help='The port on which the stress server (in GKE '
                  'containers) listens')
argp.add_argument('--stress_client_metrics_port',
                  default=_DEFAULT_METRICS_PORT,
                  type=int,
                  help='The port on which the stress clients (in GKE '
                  'containers) expose metrics')
argp.add_argument('--stress_client_poll_interval_secs',
                  default=_DEFAULT_STRESS_CLIENT_POLL_INTERVAL_SECS,
                  type=int,
                  help='How frequently should the stress client wrapper script'
                  ' running inside GKE should monitor health of the actual '
                  ' stress client process and upload the metrics to BigQuery')
argp.add_argument('--stress_client_metrics_collection_interval_secs',
                  default=_DEFAULT_METRICS_COLLECTION_INTERVAL_SECS,
                  type=int,
                  help='How frequently should metrics be collected in-memory on'
                  ' the stress clients (running inside GKE containers). Note '
                  'that this is NOT the same as the upload-to-BigQuery '
                  'frequency. The metrics upload frequency is controlled by the'
                  ' --stress_client_poll_interval_secs flag')
argp.add_argument('--stress_client_num_channels_per_server',
                  default=_DEFAULT_NUM_CHANNELS_PER_SERVER,
                  type=int,
                  help='The number of channels created to each server from a '
                  'stress client')
argp.add_argument('--stress_client_num_stubs_per_channel',
                  default=_DEFAULT_NUM_STUBS_PER_CHANNEL,
                  type=int,
                  help='The number of stubs created per channel. This number '
                  'indicates the max number of RPCs that can be made in '
                  'parallel on each channel at any given time')
argp.add_argument('--stress_client_test_cases',
                  default=_DEFAULT_TEST_CASES_STR,
                  help='List of test cases (with weights) to be executed by the'
                  ' stress test client. The list is in the following format:\n'
                  '  <testcase_1:w_1,<test_case2:w_2>..<testcase_n:w_n>\n'
                  ' (Note: The weights do not have to add up to 100)')

if __name__ == '__main__':
  args = argp.parse_args()

  test_settings = TestSettings(
      args.build_docker_image, args.test_poll_interval_secs,
      args.test_duration_secs, args.kubernetes_proxy_port)

  gke_settings = GkeSettings(args.project_id, args.docker_image_name)

  stress_server_settings = StressServerSettings(_SERVER_POD_NAME,
                                                args.stress_server_port)
  stress_client_settings = StressClientSettings(
      args.num_clients, _CLIENT_POD_NAME_PREFIX, _SERVER_POD_NAME,
      args.stress_server_port, args.stress_client_metrics_port,
      args.stress_client_metrics_collection_interval_secs,
      args.stress_client_poll_interval_secs,
      args.stress_client_num_channels_per_server,
      args.stress_client_num_stubs_per_channel, args.stress_client_test_cases)

  run_test_main(test_settings, gke_settings, stress_server_settings,
                stress_client_settings)
