#!/bin/bash
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


main() {
  source grpc_docker.sh
  # temporarily remove ping_pong and cancel_after_first_response while investigating timeout
  test_cases=(large_unary empty_unary client_streaming server_streaming cancel_after_begin)
  auth_test_cases=(service_account_creds compute_engine_creds)
  clients=(cxx java go ruby node csharp_mono)
  for test_case in "${test_cases[@]}"
  do
    for client in "${clients[@]}"
    do
      if grpc_cloud_prod_test $test_case grpc-docker-testclients $client
      then
        echo "$test_case $client $server passed" >> /tmp/cloud_prod_result.txt
      else
        echo "$test_case $client $server failed" >> /tmp/cloud_prod_result.txt
      fi
    done
  done
  for test_case in "${auth_test_cases[@]}"
  do
    for client in "${clients[@]}"
    do
      if grpc_cloud_prod_auth_test $test_case grpc-docker-testclients $client
      then
        echo "$test_case $client $server passed" >> /tmp/cloud_prod_result.txt
      else
        echo "$test_case $client $server failed" >> /tmp/cloud_prod_result.txt
      fi
    done
  done
  gsutil cp /tmp/cloud_prod_result.txt gs://stoked-keyword-656-output/cloud_prod_result.txt
  rm /tmp/cloud_prod_result.txt
}

set -x
main "$@"
