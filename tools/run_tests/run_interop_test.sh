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

language=$1
test_case=$2

# change dir gRPC repo root
cd $(dirname $0)/../..

set -e
if [ "$language" = "c++" ]
then
  bins/opt/interop_client --enable_ssl --use_prod_roots --server_host_override=grpc-test.sandbox.google.com --server_host=grpc-test.sandbox.google.com --server_port=443 --test_case=$test_case
elif [ "$language" = "node" ]
then
  SSL_CERT_FILE=/usr/local/share/grpc/roots.pem node src/node/interop/interop_client.js --use_tls=true --server_port=443 --server_host=grpc-test.sandbox.google.com --server_host_override=grpc-test.sandbox.google.com --test_case=$test_case
elif [ "$language" = "ruby" ]
then
  SSL_CERT_FILE=/usr/local/share/grpc/roots.pem ruby src/ruby/bin/interop/interop_client.rb --use_tls --server_port=443 --server_host=grpc-test.sandbox.google.com --server_host_override=grpc-test.sandbox.google.com --test_case=$test_case
elif [ "$language" = "csharp" ]
then
  (cd src/csharp/Grpc.IntegrationTesting.Client/bin/Debug && SSL_CERT_FILE=/usr/local/share/grpc/roots.pem mono Grpc.IntegrationTesting.Client.exe --use_tls --server_port=443 --server_host=grpc-test.sandbox.google.com --server_host_override=grpc-test.sandbox.google.com --test_case=$test_case)
elif [ "$language" = "php" ]
then
  SSL_CERT_FILE=/usr/local/share/grpc/roots.pem src/php/bin/interop_client.sh --server_port=443 --server_host=grpc-test.sandbox.google.com --server_host_override=grpc-test.sandbox.google.com --test_case=$test_case
else
  echo "interop tests not added for $language"
  exit 1
fi

