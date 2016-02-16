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

import argparse

import kubernetes_api

argp = argparse.ArgumentParser(description='Launch Stress tests in GKE')

argp.add_argument('-n',
                  '--num_instances',
                  required=True,
                  type=int,
                  help='The number of instances to launch in GKE')
args = argp.parse_args()

kubernetes_api_server="localhost"
kubernetes_api_port=8001


# Docker image
image_name="gcr.io/sree-gce/grpc_stress_test_2"

server_address = "stress-server.default.svc.cluster.local:8080"
metrics_server_address = "localhost:8081"

stress_test_arg_list=[
    "--server_addresses=" + server_address,
    "--test_cases=empty_unary:20,large_unary:20",
    "--num_stubs_per_channel=10"
]

metrics_client_arg_list=[
    "--metrics_server_address=" + metrics_server_address,
    "--total_only=true"]

env_dict={
    "GPRC_ROOT": "/var/local/git/grpc",
    "STRESS_TEST_IMAGE": "/var/local/git/grpc/bins/opt/stress_test",
    "STRESS_TEST_ARGS_STR": ' '.join(stress_test_arg_list),
    "METRICS_CLIENT_IMAGE": "/var/local/git/grpc/bins/opt/metrics_client",
    "METRICS_CLIENT_ARGS_STR": ' '.join(metrics_client_arg_list)}

cmd_list=["/var/local/git/grpc/bins/opt/stress_test"]
arg_list=stress_test_arg_list # make this [] in future
port_list=[8081]

namespace = 'default'
is_headless_service = False # Client is NOT headless service

print('Creating %d instances of client..' % args.num_instances)

for i in range(1, args.num_instances + 1):
  service_name = 'stress-client-%d' % i
  pod_name = service_name  # Use the same name for kubernetes Service and Pod
  is_success = kubernetes_api.create_pod(
      kubernetes_api_server,
      kubernetes_api_port,
      namespace,
      pod_name,
      image_name,
      port_list,
      cmd_list,
      arg_list,
      env_dict)
  if not is_success:
    print("Error in creating pod %s" % pod_name)
  else:
    is_success = kubernetes_api.create_service(
      kubernetes_api_server,
      kubernetes_api_port,
      namespace,
      service_name,
      pod_name,
      port_list,  # Service port list
      port_list,  # Container port list (same as service port list)
      is_headless_service)
    if not is_success:
      print("Error in creating service %s" % service_name)
    else:
      print("Created client %s" % pod_name)
