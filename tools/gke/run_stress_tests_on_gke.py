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
import datetime
import os
import subprocess
import sys
import time

stress_test_utils_dir = os.path.abspath(os.path.join(
    os.path.dirname(__file__), '../run_tests/stress_test'))
sys.path.append(stress_test_utils_dir)
from stress_test_utils import BigQueryHelper

import kubernetes_api

GRPC_ROOT = os.path.abspath(os.path.join(os.path.dirname(sys.argv[0]), '../..'))
os.chdir(GRPC_ROOT)


class BigQuerySettings:

  def __init__(self, run_id, dataset_id, summary_table_id, qps_table_id):
    self.run_id = run_id
    self.dataset_id = dataset_id
    self.summary_table_id = summary_table_id
    self.qps_table_id = qps_table_id


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
      self.p.kill()


def _build_docker_image(image_name, tag_name):
  """ Build the docker image and add a tag """
  os.environ['INTEROP_IMAGE'] = image_name
  # Note that 'BASE_NAME' HAS to be 'grpc_interop_stress_cxx' since the script
  # build_interop_stress_image.sh invokes the following script:
  #   tools/dockerfile/$BASE_NAME/build_interop_stress.sh
  os.environ['BASE_NAME'] = 'grpc_interop_stress_cxx'
  cmd = ['tools/jenkins/build_interop_stress_image.sh']
  p = subprocess.Popen(args=cmd)
  retcode = p.wait()
  if retcode != 0:
    print 'Error in building docker image'
    return False

  cmd = ['docker', 'tag', '-f', image_name, tag_name]
  p = subprocess.Popen(args=cmd)
  retcode = p.wait()
  if retcode != 0:
    print 'Error in creating the tag %s for %s' % (tag_name, image_name)
    return False

  return True


def _push_docker_image_to_gke_registry(docker_tag_name):
  """Executes 'gcloud docker push <docker_tag_name>' to push the image to GKE registry"""
  cmd = ['gcloud', 'docker', 'push', docker_tag_name]
  print 'Pushing %s to GKE registry..' % docker_tag_name
  p = subprocess.Popen(args=cmd)
  retcode = p.wait()
  if retcode != 0:
    print 'Error in pushing docker image %s to the GKE registry' % docker_tag_name
    return False
  return True


def _launch_image_on_gke(kubernetes_api_server, kubernetes_api_port, namespace,
                         pod_name, image_name, port_list, cmd_list, arg_list,
                         env_dict, is_headless_service):
  """Creates a GKE Pod and a Service object for a given image by calling Kubernetes API"""
  is_success = kubernetes_api.create_pod(
      kubernetes_api_server,
      kubernetes_api_port,
      namespace,
      pod_name,
      image_name,
      port_list,  # The ports to be exposed on this container/pod
      cmd_list,  # The command that launches the stress server
      arg_list,
      env_dict  # Environment variables to be passed to the pod
  )
  if not is_success:
    print 'Error in creating Pod'
    return False

  is_success = kubernetes_api.create_service(
      kubernetes_api_server,
      kubernetes_api_port,
      namespace,
      pod_name,  # Use the pod name for service name as well
      pod_name,
      port_list,  # Service port list
      port_list,  # Container port list (same as service port list)
      is_headless_service)
  if not is_success:
    print 'Error in creating Service'
    return False

  print 'Successfully created the pod/service %s' % pod_name
  return True


def _delete_image_on_gke(kubernetes_proxy, pod_name_list):
  """Deletes a GKE Pod and Service object for given list of Pods by calling Kubernetes API"""
  if not kubernetes_proxy.is_started:
    print 'Kubernetes proxy must be started before calling this function'
    return False

  is_success = True
  for pod_name in pod_name_list:
    is_success = kubernetes_api.delete_pod(
        'localhost', kubernetes_proxy.get_port(), 'default', pod_name)
    if not is_success:
      print 'Error in deleting pod %s' % pod_name
      break

    is_success = kubernetes_api.delete_service(
        'localhost', kubernetes_proxy.get_port(), 'default',
        pod_name)  # service name same as pod name
    if not is_success:
      print 'Error in deleting service %s' % pod_name
      break

  if is_success:
    print 'Successfully deleted the Pods/Services: %s' % ','.join(pod_name_list)

  return is_success


def _launch_server(gcp_project_id, docker_image_name, bq_settings,
                   kubernetes_proxy, server_pod_name, server_port):
  """ Launches a stress test server instance in GKE cluster """
  if not kubernetes_proxy.is_started:
    print 'Kubernetes proxy must be started before calling this function'
    return False

  server_cmd_list = [
      '/var/local/git/grpc/tools/run_tests/stress_test/run_server.py'
  ]  # Process that is launched
  server_arg_list = []  # run_server.py does not take any args (for now)

  # == Parameters to the server process launched in GKE ==
  server_env = {
      'STRESS_TEST_IMAGE_TYPE': 'SERVER',
      'STRESS_TEST_IMAGE': '/var/local/git/grpc/bins/opt/interop_server',
      'STRESS_TEST_ARGS_STR': '--port=%s' % server_port,
      'RUN_ID': bq_settings.run_id,
      'POD_NAME': server_pod_name,
      'GCP_PROJECT_ID': gcp_project_id,
      'DATASET_ID': bq_settings.dataset_id,
      'SUMMARY_TABLE_ID': bq_settings.summary_table_id,
      'QPS_TABLE_ID': bq_settings.qps_table_id
  }

  # Launch Server
  is_success = _launch_image_on_gke(
      'localhost',
      kubernetes_proxy.get_port(),
      'default',
      server_pod_name,
      docker_image_name,
      [server_port],  # Port that should be exposed on the container
      server_cmd_list,
      server_arg_list,
      server_env,
      True  # Headless = True for server. Since we want DNS records to be greated by GKE
  )

  return is_success


def _launch_client(gcp_project_id, docker_image_name, bq_settings,
                   kubernetes_proxy, num_instances, client_pod_name_prefix,
                   server_pod_name, server_port):
  """ Launches a configurable number of stress test clients on GKE cluster """
  if not kubernetes_proxy.is_started:
    print 'Kubernetes proxy must be started before calling this function'
    return False

  server_address = '%s.default.svc.cluster.local:%d' % (server_pod_name,
                                                        server_port)
  #TODO(sree) Make the whole client args configurable
  test_cases_str = 'empty_unary:1,large_unary:1'
  stress_client_arg_list = [
      '--server_addresses=%s' % server_address,
      '--test_cases=%s' % test_cases_str, '--num_stubs_per_channel=10'
  ]

  client_cmd_list = [
      '/var/local/git/grpc/tools/run_tests/stress_test/run_client.py'
  ]
  # run_client.py takes no args. All args are passed as env variables
  client_arg_list = []

  # TODO(sree) Make this configurable (and also less frequent)
  poll_interval_secs = 30

  metrics_port = 8081
  metrics_server_address = 'localhost:%d' % metrics_port
  metrics_client_arg_list = [
      '--metrics_server_address=%s' % metrics_server_address,
      '--total_only=true'
  ]

  client_env = {
      'STRESS_TEST_IMAGE_TYPE': 'CLIENT',
      'STRESS_TEST_IMAGE': '/var/local/git/grpc/bins/opt/stress_test',
      'STRESS_TEST_ARGS_STR': ' '.join(stress_client_arg_list),
      'METRICS_CLIENT_IMAGE': '/var/local/git/grpc/bins/opt/metrics_client',
      'METRICS_CLIENT_ARGS_STR': ' '.join(metrics_client_arg_list),
      'RUN_ID': bq_settings.run_id,
      'POLL_INTERVAL_SECS': str(poll_interval_secs),
      'GCP_PROJECT_ID': gcp_project_id,
      'DATASET_ID': bq_settings.dataset_id,
      'SUMMARY_TABLE_ID': bq_settings.summary_table_id,
      'QPS_TABLE_ID': bq_settings.qps_table_id
  }

  for i in range(1, num_instances + 1):
    pod_name = '%s-%d' % (client_pod_name_prefix, i)
    client_env['POD_NAME'] = pod_name
    is_success = _launch_image_on_gke(
        'localhost',
        kubernetes_proxy.get_port(),
        'default',
        pod_name,
        docker_image_name,
        [metrics_port],  # Client pods expose metrics port
        client_cmd_list,
        client_arg_list,
        client_env,
        False  # Client is not a headless service.
    )
    if not is_success:
      print 'Error in launching client %s' % pod_name
      return False

  return True


def _launch_server_and_client(bq_settings, gcp_project_id, docker_image_name,
                              num_client_instances):
  # Start kubernetes proxy
  kubernetes_api_port = 9001
  kubernetes_proxy = KubernetesProxy(kubernetes_api_port)
  kubernetes_proxy.start()

  # num of seconds to wait for the GKE image to start and warmup
  image_warmp_secs = 60

  server_pod_name = 'stress-server'
  server_port = 8080
  is_success = _launch_server(gcp_project_id, docker_image_name, bq_settings,
                              kubernetes_proxy, server_pod_name, server_port)
  if not is_success:
    print 'Error in launching server'
    return False

  # Server takes a while to start.
  # TODO(sree) Use Kubernetes API to query the status of the server instead of
  # sleeping
  print 'Waiting for %s seconds for the server to start...' % image_warmp_secs
  time.sleep(image_warmp_secs)

  # Launch client
  server_address = '%s.default.svc.cluster.local:%d' % (server_pod_name,
                                                        server_port)
  client_pod_name_prefix = 'stress-client'
  is_success = _launch_client(gcp_project_id, docker_image_name, bq_settings,
                              kubernetes_proxy, num_client_instances,
                              client_pod_name_prefix, server_pod_name,
                              server_port)
  if not is_success:
    print 'Error in launching client(s)'
    return False

  print 'Waiting for %s seconds for the client images to start...' % image_warmp_secs
  time.sleep(image_warmp_secs)
  return True


def _delete_server_and_client(num_client_instances):
  kubernetes_api_port = 9001
  kubernetes_proxy = KubernetesProxy(kubernetes_api_port)
  kubernetes_proxy.start()

  # Delete clients first
  client_pod_names = ['stress-client-%d' % i
                      for i in range(1, num_client_instances + 1)]

  is_success = _delete_image_on_gke(kubernetes_proxy, client_pod_names)
  if not is_success:
    return False

  # Delete server
  server_pod_name = 'stress-server'
  return _delete_image_on_gke(kubernetes_proxy, [server_pod_name])


def _build_and_push_docker_image(gcp_project_id, docker_image_name, tag_name):
  is_success = _build_docker_image(docker_image_name, tag_name)
  if not is_success:
    return False
  return _push_docker_image_to_gke_registry(tag_name)


# TODO(sree): This is just to test the above APIs. Rewrite this to make
# everything configurable (like image names / number of instances etc)
def run_test(skip_building_image, gcp_project_id, image_name, tag_name,
             num_client_instances, poll_interval_secs, total_duration_secs):
  if not skip_building_image:
    is_success = _build_docker_image(image_name, tag_name)
    if not is_success:
      return False

    is_success = _push_docker_image_to_gke_registry(tag_name)
    if not is_success:
      return False

  # == Big Query tables related settings (Common for both server and client) ==

  # Create a unique id for this run (Note: Using timestamp instead of UUID to
  # make it easier to deduce the date/time of the run just by looking at the run
  # run id. This is useful in debugging when looking at records in Biq query)
  run_id = datetime.datetime.now().strftime('%Y_%m_%d_%H_%M_%S')
  dataset_id = 'stress_test_%s' % run_id
  summary_table_id = 'summary'
  qps_table_id = 'qps'
  bq_settings = BigQuerySettings(run_id, dataset_id, summary_table_id,
                                 qps_table_id)

  bq_helper = BigQueryHelper(run_id, '', '', gcp_project_id, dataset_id,
                             summary_table_id, qps_table_id)
  bq_helper.initialize()
  is_success = _launch_server_and_client(bq_settings, gcp_project_id, tag_name,
                                         num_client_instances)
  if not is_success:
    return False

  start_time = datetime.datetime.now()
  end_time = start_time + datetime.timedelta(seconds=total_duration_secs)

  while True:
    if datetime.datetime.now() > end_time:
      print 'Test was run for %d seconds' % total_duration_secs
      break

    # Check if either stress server or clients have failed
    if bq_helper.check_if_any_tests_failed():
      is_success = False
      print 'Some tests failed.'
      break
    # Things seem to be running fine. Wait until next poll time to check the
    # status
    print 'Sleeping for %d seconds..' % poll_interval_secs
    time.sleep(poll_interval_secs)

  # Print BiqQuery tables
  bq_helper.print_summary_records()
  bq_helper.print_qps_records()

  _delete_server_and_client(num_client_instances)
  return is_success


if __name__ == '__main__':
  image_name = 'grpc_stress_test'
  gcp_project_id = 'sree-gce'
  tag_name = 'gcr.io/%s/%s' % (gcp_project_id, image_name)
  num_client_instances = 3
  poll_interval_secs = 10
  test_duration_secs = 150
  run_test(True, gcp_project_id, image_name, tag_name, num_client_instances,
           poll_interval_secs, test_duration_secs)
