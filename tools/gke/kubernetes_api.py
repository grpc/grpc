#!/usr/bin/env python2.7
# Copyright 2015, Google Inc.
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

import requests
import json

_REQUEST_TIMEOUT_SECS = 10

def _make_pod_config(pod_name, image_name, container_port_list, cmd_list,
                    arg_list):
  """Creates a string containing the Pod defintion as required by the Kubernetes API"""
  body = {
      'kind': 'Pod',
      'apiVersion': 'v1',
      'metadata': {
          'name': pod_name,
          'labels': {'name': pod_name}
      },
      'spec': {
          'containers': [
              {
                  'name': pod_name,
                  'image': image_name,
                  'ports': []
              }
          ]
      }
  }
  # Populate the 'ports' list
  for port in container_port_list:
    port_entry = {'containerPort': port, 'protocol': 'TCP'}
    body['spec']['containers'][0]['ports'].append(port_entry)

  # Add the 'Command' and 'Args' attributes if they are passed.
  # Note:
  #  - 'Command' overrides the ENTRYPOINT in the Docker Image
  #  - 'Args' override the COMMAND in Docker image (yes, it is confusing!)
  if len(cmd_list) > 0:
    body['spec']['containers'][0]['command'] = cmd_list
  if len(arg_list) > 0:
    body['spec']['containers'][0]['args'] = arg_list
  return json.dumps(body)


def _make_service_config(service_name, pod_name, service_port_list,
                        container_port_list, is_headless):
  """Creates a string containing the Service definition as required by the Kubernetes API.

  NOTE:
  This creates either a Headless Service or 'LoadBalancer' service depending on
  the is_headless parameter. For Headless services, there is no 'type' attribute
  and the 'clusterIP' attribute is set to 'None'. Also, if the service is
  Headless, Kubernetes creates DNS entries for Pods - i.e creates DNS A-records
  mapping the service's name to the Pods' IPs
  """
  if len(container_port_list) != len(service_port_list):
    print(
        'ERROR: container_port_list and service_port_list must be of same size')
    return ''
  body = {
      'kind': 'Service',
      'apiVersion': 'v1',
      'metadata': {
          'name': service_name,
          'labels': {
              'name': service_name
          }
      },
      'spec': {
          'ports': [],
          'selector': {
              'name': pod_name
          }
      }
  }
  # Populate the 'ports' list in the 'spec' section. This maps service ports
  # (port numbers that are exposed by Kubernetes) to container ports (i.e port
  # numbers that are exposed by your Docker image)
  for idx in range(len(container_port_list)):
    port_entry = {
        'port': service_port_list[idx],
        'targetPort': container_port_list[idx],
        'protocol': 'TCP'
    }
    body['spec']['ports'].append(port_entry)

  # Make this either a LoadBalancer service or a headless service depending on
  # the is_headless parameter
  if is_headless:
    body['spec']['clusterIP'] = 'None'
  else:
    body['spec']['type'] = 'LoadBalancer'
  return json.dumps(body)


def _print_connection_error(msg):
  print('ERROR: Connection failed. Did you remember to run Kubenetes proxy on '
        'localhost (i.e kubectl proxy --port=<proxy_port>) ?. Error: %s' % msg)

def _do_post(post_url, api_name, request_body):
  """Helper to do HTTP POST.

  Note:
  1) On success, Kubernetes returns a success code of 201(CREATED) not 200(OK)
  2) A response code of 509(CONFLICT) is interpreted as a success code (since
  the error is most likely due to the resource already existing). This makes
  _do_post() idempotent which is semantically desirable.
  """
  is_success = True
  try:
    r = requests.post(post_url, data=request_body, timeout=_REQUEST_TIMEOUT_SECS)
    if r.status_code == requests.codes.conflict:
      print('WARN: Looks like the resource already exists. Api: %s, url: %s' %
            (api_name, post_url))
    elif r.status_code != requests.codes.created:
      print('ERROR: %s API returned error. HTTP response: (%d) %s' %
            (api_name, r.status_code, r.text))
      is_success = False
  except(requests.exceptions.Timeout, requests.exceptions.ConnectionError) as e:
    is_success = False
    _print_connection_error(str(e))
  return is_success


def _do_delete(del_url, api_name):
  """Helper to do HTTP DELETE.

  Note: A response code of 404(NOT_FOUND) is treated as success to keep
  _do_delete() idempotent.
  """
  is_success = True
  try:
    r = requests.delete(del_url, timeout=_REQUEST_TIMEOUT_SECS)
    if r.status_code == requests.codes.not_found:
      print('WARN: The resource does not exist. Api: %s, url: %s' %
            (api_name, del_url))
    elif r.status_code != requests.codes.ok:
      print('ERROR: %s API returned error. HTTP response: %s' %
            (api_name, r.text))
      is_success = False
  except(requests.exceptions.Timeout, requests.exceptions.ConnectionError) as e:
    is_success = False
    _print_connection_error(str(e))
  return is_success


def create_service(kube_host, kube_port, namespace, service_name, pod_name,
                   service_port_list, container_port_list, is_headless):
  """Creates either a Headless Service or a LoadBalancer Service depending
  on the is_headless parameter.
  """
  post_url = 'http://%s:%d/api/v1/namespaces/%s/services' % (
      kube_host, kube_port, namespace)
  request_body = _make_service_config(service_name, pod_name, service_port_list,
                                     container_port_list, is_headless)
  return _do_post(post_url, 'Create Service', request_body)


def create_pod(kube_host, kube_port, namespace, pod_name, image_name,
               container_port_list, cmd_list, arg_list):
  """Creates a Kubernetes Pod.

  Note that it is generally NOT considered a good practice to directly create
  Pods. Typically, the recommendation is to create 'Controllers' to create and
  manage Pods' lifecycle. Currently Kubernetes only supports 'Replication
  Controller' which creates a configurable number of 'identical Replicas' of
  Pods and automatically restarts any Pods in case of failures (for eg: Machine
  failures in Kubernetes). This makes it less flexible for our test use cases
  where we might want slightly different set of args to each Pod. Hence we
  directly create Pods and not care much about Kubernetes failures since those
  are very rare.
  """
  post_url = 'http://%s:%d/api/v1/namespaces/%s/pods' % (kube_host, kube_port,
                                                         namespace)
  request_body = _make_pod_config(pod_name, image_name, container_port_list,
                                 cmd_list, arg_list)
  return _do_post(post_url, 'Create Pod', request_body)


def delete_service(kube_host, kube_port, namespace, service_name):
  del_url = 'http://%s:%d/api/v1/namespaces/%s/services/%s' % (
      kube_host, kube_port, namespace, service_name)
  return _do_delete(del_url, 'Delete Service')


def delete_pod(kube_host, kube_port, namespace, pod_name):
  del_url = 'http://%s:%d/api/v1/namespaces/%s/pods/%s' % (kube_host, kube_port,
                                                           namespace, pod_name)
  return _do_delete(del_url, 'Delete Pod')
