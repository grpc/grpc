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

thisfile=$(readlink -ne "${BASH_SOURCE[0]}")
cur=$(date "+%Y-%m-%d-%H-%M-%S")
log_link=https://pantheon.corp.google.com/m/cloudstorage/b/stoked-keyword-656-output/o/prod_result/prod/$cur/logs

main() {
  source grpc_docker.sh
  test_cases=(large_unary empty_unary ping_pong client_streaming server_streaming cancel_after_begin cancel_after_first_response)
  auth_test_cases=(service_account_creds compute_engine_creds jwt_token_creds)
  clients=(cxx java go ruby node csharp_mono python php)
  for test_case in "${test_cases[@]}"
  do
    for client in "${clients[@]}"
    do
      log_file_name=cloud_{$test_case}_{$client}.txt 
      if grpc_cloud_prod_test $test_case grpc-docker-testclients $client > /tmp/$log_file_name 2>&1
      then
        echo "          ['$test_case', '$client', 'prod', true, '<a href="$log_link/$log_file_name">log</a>']," >> /tmp/cloud_prod_result.txt
      else
        echo "          ['$test_case', '$client', 'prod', false, '<a href="$log_link/$log_file_name">log</a>']," >> /tmp/cloud_prod_result.txt
      fi
      gsutil cp /tmp/$log_file_name gs://stoked-keyword-656-output/prod_result/prod/$cur/logs/$log_file_name
      rm /tmp/$log_file_name
    done
  done
  for test_case in "${auth_test_cases[@]}"
  do
    for client in "${clients[@]}"
    do
      log_file_name=cloud_{$test_case}_{$client}.txt 
      if grpc_cloud_prod_auth_test $test_case grpc-docker-testclients $client > /tmp/$log_file_name 2>&1
      then
        echo "          ['$test_case', '$client', 'prod', true, '<a href="$log_link/$log_file_name">log</a>']," >> /tmp/cloud_prod_result.txt
      else
        echo "          ['$test_case', '$client', 'prod', false, '<a href="$log_link/$log_file_name">log</a>']," >> /tmp/cloud_prod_result.txt
      fi
      gsutil cp /tmp/$log_file_name gs://stoked-keyword-656-output/prod_result/prod/$cur/logs/$log_file_name
      rm /tmp/$log_file_name
    done
  done
  if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    cat pre.html /tmp/cloud_prod_result.txt post.html > /tmp/cloud_prod_result.html
    gsutil cp /tmp/cloud_prod_result.html gs://stoked-keyword-656-output/prod_result/prod/$cur/cloud_prod_result.html
    rm /tmp/cloud_prod_result.txt
    rm /tmp/cloud_prod_result.html
  fi
}

set -x
main "$@"
