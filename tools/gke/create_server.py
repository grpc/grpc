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

service_name = 'stress-server'
pod_name = service_name  # Use the same name for kubernetes Service and Pod
namespace = 'default'
is_headless_service = True
cmd_list=['/var/local/git/grpc/bins/opt/interop_server']
arg_list=['--port=8080']
port_list=[8080]
image_name='gcr.io/sree-gce/grpc_stress_test_2'
env_dict={}

# Make sure you run kubectl proxy --port=8001
kubernetes_api_server='localhost'
kubernetes_api_port=8001

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
  print("Error in creating pod")
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
    print("Error in creating service")
  else:
    print("Successfully created the Server")
