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

NODE_VERSION=$1
source ~/.nvm/nvm.sh

nvm use $NODE_VERSION
set -ex

CONFIG=${CONFIG:-opt}

# change to grpc repo root
cd $(dirname $0)/../../..

case "$CONFIG" in
  'dbg') config_flags='--debug' ;;
  'gcov') config_flags="--debug --grpc_gcov=true" ;;
  *) config_flag='--release' ;;
esac

npm install --unsafe-perm --build-from-source $config_flag
