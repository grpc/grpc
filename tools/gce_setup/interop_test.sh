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
test_case=$1
client_vm=$2
server_vm=$3
result=interop_result.$1
cur=$(date "+%Y-%m-%d-%H-%M-%S") 
log_link=https://pantheon.corp.google.com/m/cloudstorage/b/stoked-keyword-656-output/o/interop_result/$test_case/$cur

main() {
  source grpc_docker.sh
  clients=(cxx java go ruby node csharp_mono python php)
  servers=(cxx java go ruby node python csharp_mono)
  for client in "${clients[@]}"
  do
    for server in "${servers[@]}"
    do
      log_file_name=cloud_{$test_case}_{$client}_{$server}.txt 
      if grpc_interop_test $test_case $client_vm $client $server_vm $server> /tmp/$log_file_name 2>&1
      then
        echo "          ['$test_case', '$client', '$server', true, '<a href="$log_link/$log_file_name">log</a>']," >> /tmp/$result.txt
      else
        echo "          ['$test_case', '$client', '$server', false, '<a href="$log_link/$log_file_name">log</a>']," >> /tmp/$result.txt
      fi
      gsutil cp /tmp/$log_file_name gs://stoked-keyword-656-output/interop_result/$test_case/$cur/$log_file_name
      rm /tmp/$log_file_name
    done
  done
  if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    cat pre.html /tmp/$result.txt post.html > /tmp/$result.html
    gsutil cp /tmp/$result.html gs://stoked-keyword-656-output/interop_result/$test_case/$cur/$result.html
    rm /tmp/$result.txt
    rm /tmp/$result.html
  fi
}

set -x
main "$@"
