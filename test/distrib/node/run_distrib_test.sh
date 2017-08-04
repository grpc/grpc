#!/bin/bash
# Copyright 2015 gRPC authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

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
# docker and use 12345
STATIC_PORT=$(curl 'localhost:32767/get' || echo '12345')

# Serves the input_artifacts directory statically at localhost:
static "$EXTERNAL_GIT_ROOT/input_artifacts" -a $STATIC_SERVER -p $STATIC_PORT &
STATIC_PID=$!

STATIC_URL="http://$STATIC_SERVER:$STATIC_PORT/"

npm install --unsafe-perm $STATIC_URL/grpc.tgz --grpc_node_binary_host_mirror=$STATIC_URL

./distrib_test.js
