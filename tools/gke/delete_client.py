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

argp = argparse.ArgumentParser(description='Delete Stress test clients in GKE')
argp.add_argument('-n',
                  '--num_instances',
                  required=True,
                  type=int,
                  help='The number of instances currently running')

args = argp.parse_args()
for i in range(1, args.num_instances + 1):
  service_name = 'stress-client-%d' % i
  pod_name = service_name
  namespace = 'default'
  kubernetes_api_server="localhost"
  kubernetes_api_port=8001

  is_success=kubernetes_api.delete_pod(
      kubernetes_api_server,
      kubernetes_api_port,
      namespace,
      pod_name)
  if not is_success:
    print('Error in deleting Pod %s' % pod_name)
  else:
    is_success= kubernetes_api.delete_service(
      kubernetes_api_server,
      kubernetes_api_port,
      namespace,
      service_name)
    if not is_success:
      print('Error in deleting Service %s' % service_name)
    else:
      print('Deleted %s' % pod_name)
