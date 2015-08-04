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
current_time=$(date "+%Y-%m-%d-%H-%M-%S")
result_file_name=interop_result.$current_time.html
echo $result_file_name
pass_log_link=https://pantheon.corp.google.com/m/cloudstorage/b/stoked-keyword-656-output/o/log/interop_pass_log_history
fail_log_link=https://pantheon.corp.google.com/m/cloudstorage/b/stoked-keyword-656-output/o/log/interop_fail_log_history

main() {
  source grpc_docker.sh
  test_cases=(large_unary empty_unary ping_pong client_streaming server_streaming cancel_after_begin cancel_after_first_response empty_stream timeout_on_sleeping_server)
  clients=(cxx java go ruby node python csharp_mono php)
  servers=(cxx java go ruby node python csharp_mono)
  for test_case in "${test_cases[@]}"
  do
    for client in "${clients[@]}"
    do
      for server in "${servers[@]}"
      do
        log_file_name=interop_{$test_case}_{$client}_{$server}.txt
        if grpc_interop_test $test_case grpc-docker-testclients $client grpc-docker-server $server > /tmp/$log_file_name 2>&1
        then
          gsutil cp /tmp/$log_file_name gs://stoked-keyword-656-output/interop_pass_log_history/$log_file_name
          echo "          ['$test_case', '$client', '$server', true, '<a href="$pass_log_link/$log_file_name">log</a>']," >> /tmp/interop_result.txt
        else
          gsutil cp /tmp/$log_file_name gs://stoked-keyword-656-output/interop_fail_log_history/$log_file_name
          echo "          ['$test_case', '$client', '$server', false, '<a href="$fail_log_link/$log_file_name">log</a>']," >> /tmp/interop_result.txt
        fi
      done
    done
  done
  if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    cat pre.html /tmp/interop_result.txt post.html > /tmp/interop_result.html
    gsutil cp /tmp/interop_result.txt gs://stoked-keyword-656-output/interop_result.txt
    gsutil cp -R gs://stoked-keyword-656-output/interop_pass_log_history gs://stoked-keyword-656-output/log
    gsutil cp -R gs://stoked-keyword-656-output/interop_fail_log_history gs://stoked-keyword-656-output/log
    gsutil cp /tmp/interop_result.html gs://stoked-keyword-656-output/interop_result.html
    gsutil cp /tmp/interop_result.html gs://stoked-keyword-656-output/result_history/$result_file_name
    rm /tmp/interop_result.txt
    rm /tmp/interop_result.html
    rm /tmp/interop*.txt
  fi
}

set -x
main "$@"
