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
import json
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


class GlobalSettings:

  def __init__(self, gcp_project_id, build_docker_images,
               test_poll_interval_secs, test_duration_secs,
               kubernetes_proxy_port, dataset_id_prefix, summary_table_id,
               qps_table_id, pod_warmup_secs):
    self.gcp_project_id = gcp_project_id
    self.build_docker_images = build_docker_images
    self.test_poll_interval_secs = test_poll_interval_secs
    self.test_duration_secs = test_duration_secs
    self.kubernetes_proxy_port = kubernetes_proxy_port
    self.dataset_id_prefix = dataset_id_prefix
    self.summary_table_id = summary_table_id
    self.qps_table_id = qps_table_id
    self.pod_warmup_secs = pod_warmup_secs


class ClientTemplate:
  """ Contains all the common settings that are used by a stress client """

  def __init__(self, name, stress_client_cmd, metrics_client_cmd, metrics_port,
               wrapper_script_path, poll_interval_secs, client_args_dict,
               metrics_args_dict, will_run_forever, env_dict):
    self.name = name
    self.stress_client_cmd = stress_client_cmd
    self.metrics_client_cmd = metrics_client_cmd
    self.metrics_port = metrics_port
    self.wrapper_script_path = wrapper_script_path
    self.poll_interval_secs = poll_interval_secs
    self.client_args_dict = client_args_dict
    self.metrics_args_dict = metrics_args_dict
    self.will_run_forever = will_run_forever
    self.env_dict = env_dict


class ServerTemplate:
  """ Contains all the common settings used by a stress server """

  def __init__(self, name, server_cmd, wrapper_script_path, server_port,
               server_args_dict, will_run_forever, env_dict):
    self.name = name
    self.server_cmd = server_cmd
    self.wrapper_script_path = wrapper_script_path
    self.server_port = server_port
    self.server_args_dict = server_args_dict
    self.will_run_forever = will_run_forever
    self.env_dict = env_dict


class DockerImage:
  """ Represents properties of a Docker image. Provides methods to build the
  image and push it to GKE registry
  """

  def __init__(self, gcp_project_id, image_name, build_script_path,
               dockerfile_dir, build_type):
    """Args:

      image_name: The docker image name
      tag_name: The additional tag name. This is the name used when pushing the
        docker image to GKE registry
      build_script_path: The path to the build script that builds this docker
      image
      dockerfile_dir: The name of the directory under
      '<grpc_root>/tools/dockerfile' that contains the dockerfile
    """
    self.image_name = image_name
    self.gcp_project_id = gcp_project_id
    self.build_script_path = build_script_path
    self.dockerfile_dir = dockerfile_dir
    self.build_type = build_type
    self.tag_name = self._make_tag_name(gcp_project_id, image_name)

  def _make_tag_name(self, project_id, image_name):
    return 'gcr.io/%s/%s' % (project_id, image_name)

  def build_image(self):
    print 'Building docker image: %s (tag: %s)' % (self.image_name,
                                                   self.tag_name)
    os.environ['INTEROP_IMAGE'] = self.image_name
    os.environ['INTEROP_IMAGE_REPOSITORY_TAG'] = self.tag_name
    os.environ['BASE_NAME'] = self.dockerfile_dir
    os.environ['BUILD_TYPE'] = self.build_type
    print 'DEBUG: path: ', self.build_script_path
    if subprocess.call(args=[self.build_script_path]) != 0:
      print 'Error in building the Docker image'
      return False
    return True

  def push_to_gke_registry(self):
    cmd = ['gcloud', 'docker', 'push', self.tag_name]
    print 'Pushing %s to the GKE registry..' % self.tag_name
    if subprocess.call(args=cmd) != 0:
      print 'Error in pushing the image %s to the GKE registry' % self.tag_name
      return False
    return True


class ServerPodSpec:
  """ Contains the information required to launch server pods. """

  def __init__(self, name, server_template, docker_image, num_instances):
    self.name = name
    self.template = server_template
    self.docker_image = docker_image
    self.num_instances = num_instances

  def pod_names(self):
    """ Return a list of names of server pods to create. """
    return ['%s-%d' % (self.name, i) for i in range(1, self.num_instances + 1)]

  def server_addresses(self):
    """ Return string of server addresses in the following format:
      '<server_pod_name_1>:<server_port>,<server_pod_name_2>:<server_port>...'
    """
    return ','.join(['%s:%d' % (pod_name, self.template.server_port)
                     for pod_name in self.pod_names()])


class ClientPodSpec:
  """ Contains the information required to launch client pods """

  def __init__(self, name, client_template, docker_image, num_instances,
               server_addresses):
    self.name = name
    self.template = client_template
    self.docker_image = docker_image
    self.num_instances = num_instances
    self.server_addresses = server_addresses

  def pod_names(self):
    """ Return a list of names of client pods to create """
    return ['%s-%d' % (self.name, i) for i in range(1, self.num_instances + 1)]

  # The client args in the template do not have server addresses. This function
  # adds the server addresses and returns the updated client args
  def get_client_args_dict(self):
    args_dict = self.template.client_args_dict.copy()
    args_dict['server_addresses'] = self.server_addresses
    return args_dict


class Gke:
  """ Class that has helper methods to interact with GKE """

  class KubernetesProxy:
    """Class to start a proxy on localhost to talk to the Kubernetes API server"""

    def __init__(self, port):
      cmd = ['kubectl', 'proxy', '--port=%d' % port]
      self.p = subprocess.Popen(args=cmd)
      time.sleep(2)
      print '\nStarted kubernetes proxy on port: %d' % port

    def __del__(self):
      if self.p is not None:
        print 'Shutting down Kubernetes proxy..'
        self.p.kill()

  def __init__(self, project_id, run_id, dataset_id, summary_table_id,
               qps_table_id, kubernetes_port):
    self.project_id = project_id
    self.run_id = run_id
    self.dataset_id = dataset_id
    self.summary_table_id = summary_table_id
    self.qps_table_id = qps_table_id

    # The environment variables we would like to pass to every pod (both client
    # and server) launched in GKE
    self.gke_env = {
        'RUN_ID': self.run_id,
        'GCP_PROJECT_ID': self.project_id,
        'DATASET_ID': self.dataset_id,
        'SUMMARY_TABLE_ID': self.summary_table_id,
        'QPS_TABLE_ID': self.qps_table_id
    }

    self.kubernetes_port = kubernetes_port
    # Start kubernetes proxy
    self.kubernetes_proxy = Gke.KubernetesProxy(kubernetes_port)

  def _args_dict_to_str(self, args_dict):
    return ' '.join('--%s=%s' % (k, args_dict[k]) for k in args_dict.keys())

  def launch_servers(self, server_pod_spec):
    is_success = True

    # The command to run inside the container is the wrapper script (which then
    # launches the actual server)
    container_cmd = server_pod_spec.template.wrapper_script_path

    # The parameters to the wrapper script (defined in
    # server_pod_spec.template.wrapper_script_path) are are injected into the
    # container via environment variables
    server_env = self.gke_env.copy()
    server_env.update(server_pod_spec.template.env_dict)
    server_env.update({
        'STRESS_TEST_IMAGE_TYPE': 'SERVER',
        'STRESS_TEST_CMD': server_pod_spec.template.server_cmd,
        'STRESS_TEST_ARGS_STR': self._args_dict_to_str(
            server_pod_spec.template.server_args_dict),
        'WILL_RUN_FOREVER': str(server_pod_spec.template.will_run_forever)
    })

    for pod_name in server_pod_spec.pod_names():
      server_env['POD_NAME'] = pod_name
      print 'Creating server: %s' % pod_name
      is_success = kubernetes_api.create_pod_and_service(
          'localhost',
          self.kubernetes_port,
          'default',  # Use 'default' namespace
          pod_name,
          server_pod_spec.docker_image.tag_name,
          [server_pod_spec.template.server_port],  # Ports to expose on the pod
          [container_cmd],
          [],  # Args list is empty since we are passing all args via env variables
          server_env,
          True  # Headless = True for server to that GKE creates a DNS record for pod_name
      )
      if not is_success:
        print 'Error in launching server: %s' % pod_name
        break

    if is_success:
      print 'Successfully created server(s)'

    return is_success

  def launch_clients(self, client_pod_spec):
    is_success = True

    # The command to run inside the container is the wrapper script (which then
    # launches the actual stress client)
    container_cmd = client_pod_spec.template.wrapper_script_path

    # The parameters to the wrapper script (defined in
    # client_pod_spec.template.wrapper_script_path) are are injected into the
    # container via environment variables
    client_env = self.gke_env.copy()
    client_env.update(client_pod_spec.template.env_dict)
    client_env.update({
        'STRESS_TEST_IMAGE_TYPE': 'CLIENT',
        'STRESS_TEST_CMD': client_pod_spec.template.stress_client_cmd,
        'STRESS_TEST_ARGS_STR': self._args_dict_to_str(
            client_pod_spec.get_client_args_dict()),
        'METRICS_CLIENT_CMD': client_pod_spec.template.metrics_client_cmd,
        'METRICS_CLIENT_ARGS_STR': self._args_dict_to_str(
            client_pod_spec.template.metrics_args_dict),
        'POLL_INTERVAL_SECS': str(client_pod_spec.template.poll_interval_secs),
        'WILL_RUN_FOREVER': str(client_pod_spec.template.will_run_forever)
    })

    for pod_name in client_pod_spec.pod_names():
      client_env['POD_NAME'] = pod_name
      print 'Creating client: %s' % pod_name
      is_success = kubernetes_api.create_pod_and_service(
          'localhost',
          self.kubernetes_port,
          'default',  # default namespace,
          pod_name,
          client_pod_spec.docker_image.tag_name,
          [client_pod_spec.template.metrics_port],  # Ports to expose on the pod
          [container_cmd],
          [],  # Empty args list since all args are passed via env variables
          client_env,
          False  # Client is not a headless service.
      )

      if not is_success:
        print 'Error in launching client %s' % pod_name
        break

    if is_success:
      print 'Successfully created all client(s)'

    return is_success

  def _delete_pods(self, pod_name_list):
    is_success = True
    for pod_name in pod_name_list:
      print 'Deleting %s' % pod_name
      is_success = kubernetes_api.delete_pod_and_service(
          'localhost',
          self.kubernetes_port,
          'default',  # default namespace
          pod_name)

      if not is_success:
        print 'Error in deleting pod %s' % pod_name
        break

    if is_success:
      print 'Successfully deleted all pods'

    return is_success

  def delete_servers(self, server_pod_spec):
    return self._delete_pods(server_pod_spec.pod_names())

  def delete_clients(self, client_pod_spec):
    return self._delete_pods(client_pod_spec.pod_names())


class Config:

  def __init__(self, config_filename, gcp_project_id):
    print 'Loading configuration...'
    config_dict = self._load_config(config_filename)

    self.global_settings = self._parse_global_settings(config_dict,
                                                       gcp_project_id)
    self.docker_images_dict = self._parse_docker_images(
        config_dict, self.global_settings.gcp_project_id)
    self.client_templates_dict = self._parse_client_templates(config_dict)
    self.server_templates_dict = self._parse_server_templates(config_dict)
    self.server_pod_specs_dict = self._parse_server_pod_specs(
        config_dict, self.docker_images_dict, self.server_templates_dict)
    self.client_pod_specs_dict = self._parse_client_pod_specs(
        config_dict, self.docker_images_dict, self.client_templates_dict,
        self.server_pod_specs_dict)
    print 'Loaded Configuaration.'

  def _parse_global_settings(self, config_dict, gcp_project_id):
    global_settings_dict = config_dict['globalSettings']
    return GlobalSettings(gcp_project_id,
                          global_settings_dict['buildDockerImages'],
                          global_settings_dict['pollIntervalSecs'],
                          global_settings_dict['testDurationSecs'],
                          global_settings_dict['kubernetesProxyPort'],
                          global_settings_dict['datasetIdNamePrefix'],
                          global_settings_dict['summaryTableId'],
                          global_settings_dict['qpsTableId'],
                          global_settings_dict['podWarmupSecs'])

  def _parse_docker_images(self, config_dict, gcp_project_id):
    """Parses the 'dockerImages' section of the config file and returns a
    Dictionary of 'DockerImage' objects keyed by docker image names"""
    docker_images_dict = {}

    docker_config_dict = config_dict['dockerImages']
    for image_name in docker_config_dict.keys():
      build_script_path = docker_config_dict[image_name]['buildScript']
      dockerfile_dir = docker_config_dict[image_name]['dockerFileDir']
      build_type = docker_config_dict[image_name].get('buildType', 'opt')
      docker_images_dict[image_name] = DockerImage(gcp_project_id, image_name,
                                                   build_script_path,
                                                   dockerfile_dir, build_type)
    return docker_images_dict

  def _parse_client_templates(self, config_dict):
    """Parses the 'clientTemplates' section of the config file and returns a
    Dictionary of 'ClientTemplate' objects keyed by client template names.

    Note: The 'baseTemplates' sub section of the config file contains templates
    with default values  and the 'templates' sub section contains the actual
    client templates (which refer to the base template name to use for default
    values).
    """
    client_templates_dict = {}

    templates_dict = config_dict['clientTemplates']['templates']
    base_templates_dict = config_dict['clientTemplates'].get('baseTemplates',
                                                             {})
    for template_name in templates_dict.keys():
      # temp_dict is a temporary dictionary that merges base template dictionary
      # and client template dictionary (with client template dictionary values
      # overriding base template values)
      temp_dict = {}

      base_template_name = templates_dict[template_name].get('baseTemplate')
      if not base_template_name is None:
        temp_dict = base_templates_dict[base_template_name].copy()

      temp_dict.update(templates_dict[template_name])

      # Create and add ClientTemplate object to the final client_templates_dict
      stress_client_cmd = ' '.join(temp_dict['stressClientCmd'])
      metrics_client_cmd = ' '.join(temp_dict['metricsClientCmd'])
      client_templates_dict[template_name] = ClientTemplate(
          template_name, stress_client_cmd, metrics_client_cmd,
          temp_dict['metricsPort'], temp_dict['wrapperScriptPath'],
          temp_dict['pollIntervalSecs'], temp_dict['clientArgs'].copy(),
          temp_dict['metricsArgs'].copy(), temp_dict.get('willRunForever', 1),
          temp_dict.get('env', {}).copy())

    return client_templates_dict

  def _parse_server_templates(self, config_dict):
    """Parses the 'serverTemplates' section of the config file and returns a
    Dictionary of 'serverTemplate' objects keyed by server template names.

    Note: The 'baseTemplates' sub section of the config file contains templates
    with default values  and the 'templates' sub section contains the actual
    server templates (which refer to the base template name to use for default
    values).
    """
    server_templates_dict = {}

    templates_dict = config_dict['serverTemplates']['templates']
    base_templates_dict = config_dict['serverTemplates'].get('baseTemplates',
                                                             {})

    for template_name in templates_dict.keys():
      # temp_dict is a temporary dictionary that merges base template dictionary
      # and server template dictionary (with server template dictionary values
      # overriding base template values)
      temp_dict = {}

      base_template_name = templates_dict[template_name].get('baseTemplate')
      if not base_template_name is None:
        temp_dict = base_templates_dict[base_template_name].copy()

      temp_dict.update(templates_dict[template_name])

      # Create and add ServerTemplate object to the final server_templates_dict
      stress_server_cmd = ' '.join(temp_dict['stressServerCmd'])
      server_templates_dict[template_name] = ServerTemplate(
          template_name, stress_server_cmd, temp_dict['wrapperScriptPath'],
          temp_dict['serverPort'], temp_dict['serverArgs'].copy(),
          temp_dict.get('willRunForever', 1), temp_dict.get('env', {}).copy())

    return server_templates_dict

  def _parse_server_pod_specs(self, config_dict, docker_images_dict,
                              server_templates_dict):
    """Parses the 'serverPodSpecs' sub-section (under 'testMatrix' section) of
    the config file and returns a Dictionary of 'ServerPodSpec' objects keyed
    by server pod spec names"""
    server_pod_specs_dict = {}

    pod_specs_dict = config_dict['testMatrix'].get('serverPodSpecs', {})

    for pod_name in pod_specs_dict.keys():
      server_template_name = pod_specs_dict[pod_name]['serverTemplate']
      docker_image_name = pod_specs_dict[pod_name]['dockerImage']
      num_instances = pod_specs_dict[pod_name].get('numInstances', 1)

      # Create and add the ServerPodSpec object to the final
      # server_pod_specs_dict
      server_pod_specs_dict[pod_name] = ServerPodSpec(
          pod_name, server_templates_dict[server_template_name],
          docker_images_dict[docker_image_name], num_instances)

    return server_pod_specs_dict

  def _parse_client_pod_specs(self, config_dict, docker_images_dict,
                              client_templates_dict, server_pod_specs_dict):
    """Parses the 'clientPodSpecs' sub-section (under 'testMatrix' section) of
    the config file and returns a Dictionary of 'ClientPodSpec' objects keyed
    by client pod spec names"""
    client_pod_specs_dict = {}

    pod_specs_dict = config_dict['testMatrix'].get('clientPodSpecs', {})
    for pod_name in pod_specs_dict.keys():
      client_template_name = pod_specs_dict[pod_name]['clientTemplate']
      docker_image_name = pod_specs_dict[pod_name]['dockerImage']
      num_instances = pod_specs_dict[pod_name]['numInstances']

      # Get the server addresses from the server pod spec object
      server_pod_spec_name = pod_specs_dict[pod_name]['serverPodSpec']
      server_addresses = server_pod_specs_dict[
          server_pod_spec_name].server_addresses()

      client_pod_specs_dict[pod_name] = ClientPodSpec(
          pod_name, client_templates_dict[client_template_name],
          docker_images_dict[docker_image_name], num_instances,
          server_addresses)

    return client_pod_specs_dict

  def _load_config(self, config_filename):
    """Opens the config file and converts the Json text to Dictionary"""
    if not os.path.isabs(config_filename):
      raise Exception('Config objects expects an absolute file path. '
                      'config file name passed: %s' % config_filename)
    with open(config_filename) as config_file:
      return json.load(config_file)


def run_tests(config):
  """ The main function that launches the stress tests """
  # Build docker images and push to GKE registry
  if config.global_settings.build_docker_images:
    for name, docker_image in config.docker_images_dict.iteritems():
      if not (docker_image.build_image() and
              docker_image.push_to_gke_registry()):
        return False

  # Create a unique id for this run (Note: Using timestamp instead of UUID to
  # make it easier to deduce the date/time of the run just by looking at the run
  # run id. This is useful in debugging when looking at records in Biq query)
  run_id = datetime.datetime.now().strftime('%Y_%m_%d_%H_%M_%S')
  dataset_id = '%s_%s' % (config.global_settings.dataset_id_prefix, run_id)
  print 'Run id:', run_id
  print 'Dataset id:', dataset_id

  bq_helper = BigQueryHelper(run_id, '', '',
                             config.global_settings.gcp_project_id, dataset_id,
                             config.global_settings.summary_table_id,
                             config.global_settings.qps_table_id)
  bq_helper.initialize()

  gke = Gke(config.global_settings.gcp_project_id, run_id, dataset_id,
            config.global_settings.summary_table_id,
            config.global_settings.qps_table_id,
            config.global_settings.kubernetes_proxy_port)

  is_success = True

  try:
    print 'Launching servers..'
    for name, server_pod_spec in config.server_pod_specs_dict.iteritems():
      if not gke.launch_servers(server_pod_spec):
        is_success = False  # is_success is checked in the 'finally' block
        return False

    print('Launched servers. Waiting for %d seconds for the server pods to be '
          'fully online') % config.global_settings.pod_warmup_secs
    time.sleep(config.global_settings.pod_warmup_secs)

    for name, client_pod_spec in config.client_pod_specs_dict.iteritems():
      if not gke.launch_clients(client_pod_spec):
        is_success = False  # is_success is checked in the 'finally' block
        return False

    print('Launched all clients. Waiting for %d seconds for the client pods to '
          'be fully online') % config.global_settings.pod_warmup_secs
    time.sleep(config.global_settings.pod_warmup_secs)

    start_time = datetime.datetime.now()
    end_time = start_time + datetime.timedelta(
        seconds=config.global_settings.test_duration_secs)
    print 'Running the test until %s' % end_time.isoformat()

    while True:
      if datetime.datetime.now() > end_time:
        print 'Test was run for %d seconds' % config.global_settings.test_duration_secs
        break

      # Check if either stress server or clients have failed (btw, the bq_helper
      # monitors all the rows in the summary table and checks if any of them
      # have a failure status)
      if bq_helper.check_if_any_tests_failed():
        is_success = False
        print 'Some tests failed.'
        break  # Don't 'return' here. We still want to call bq_helper to print qps/summary tables

      # Tests running fine. Wait until next poll time to check the status
      print 'Sleeping for %d seconds..' % config.global_settings.test_poll_interval_secs
      time.sleep(config.global_settings.test_poll_interval_secs)

    # Print BiqQuery tables
    bq_helper.print_qps_records()
    bq_helper.print_summary_records()

  finally:
    # If there was a test failure, we should not delete the pods since they
    # would contain useful debug information (logs, core dumps etc)
    if is_success:
      for name, server_pod_spec in config.server_pod_specs_dict.iteritems():
        gke.delete_servers(server_pod_spec)
      for name, client_pod_spec in config.client_pod_specs_dict.iteritems():
        gke.delete_clients(client_pod_spec)

  return is_success


def tear_down(config):
  gke = Gke(config.global_settings.gcp_project_id, '', '',
            config.global_settings.summary_table_id,
            config.global_settings.qps_table_id,
            config.global_settings.kubernetes_proxy_port)
  for name, server_pod_spec in config.server_pod_specs_dict.iteritems():
    gke.delete_servers(server_pod_spec)
  for name, client_pod_spec in config.client_pod_specs_dict.iteritems():
    gke.delete_clients(client_pod_spec)


argp = argparse.ArgumentParser(
    description='Launch stress tests in GKE',
    formatter_class=argparse.ArgumentDefaultsHelpFormatter)
argp.add_argument('--gcp_project_id',
                  required=True,
                  help='The Google Cloud Platform Project Id')
argp.add_argument('--config_file',
                  required=True,
                  type=str,
                  help='The test config file')
argp.add_argument('--tear_down', action='store_true', default=False)

if __name__ == '__main__':
  args = argp.parse_args()

  config_filename = args.config_file

  # Since we will be changing the current working directory to grpc root in the
  # next step, we should check if the config filename path is a relative path
  # (i.e a path relative to the current working directory) and if so, convert it
  # to abosulte path
  if not os.path.isabs(config_filename):
    config_filename = os.path.abspath(config_filename)

  config = Config(config_filename, args.gcp_project_id)

  # Change current working directory to grpc root
  # (This is important because all relative file paths in the config file are
  # supposed to interpreted as relative to the GRPC root)
  grpc_root = os.path.abspath(os.path.join(
      os.path.dirname(sys.argv[0]), '../../..'))
  os.chdir(grpc_root)

  # Note that tear_down is only in cases where we want to manually tear down a
  # test that for some reason run_tests() could not cleanup
  if args.tear_down:
    tear_down(config)
    sys.exit(1)

  if not run_tests(config):
    sys.exit(1)
