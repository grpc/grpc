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

function finish() {
  rv=$?
  kill $STATIC_PID || true
  curl "localhost:32767/drop/$STATIC_PORT" || true
  exit $rv
}

trap finish EXIT

NODE_VERSION=$1
source ~/.nvm/nvm.sh

cd $(dirname $0)

nvm install $NODE_VERSION
set -ex

npm install -g node-static

STATIC_SERVER=127.0.0.1
# If port_server is running, get port from that. Otherwise, assume we're in
# docker and use 8080
STATIC_PORT=$(curl 'localhost:32767/get' || echo '8080')

# Serves the input_artifacts directory statically at localhost:
static "$EXTERNAL_GIT_ROOT/input_artifacts" -a $STATIC_SERVER -p $STATIC_PORT &
STATIC_PID=$!

STATIC_URL="http://$STATIC_SERVER:$STATIC_PORT/"

npm install --unsafe-perm $STATIC_URL/grpc.tgz --grpc_node_binary_host_mirror=$STATIC_URL

./distrib_test.js
