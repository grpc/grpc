#!/bin/bash

# Copyright 2016 gRPC authors.
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

ELECTRON_VERSION=$1
source ~/.nvm/nvm.sh

nvm use 8
set -ex

# change to grpc repo root
cd $(dirname $0)/../..

export npm_config_target=$ELECTRON_VERSION
export npm_config_disturl=https://atom.io/download/atom-shell
export npm_config_runtime=electron
export npm_config_build_from_source=true
mkdir -p ~/.electron-gyp
HOME=~/.electron-gyp npm update --prefer-online
HOME=~/.electron-gyp npm install --unsafe-perm
