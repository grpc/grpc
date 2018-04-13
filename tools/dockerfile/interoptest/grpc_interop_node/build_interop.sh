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
#
# Builds Node interop server and client in a base image.
set -e

mkdir -p /var/local/git

git clone /var/local/jenkins/grpc-node /var/local/git/grpc-node
# clone gRPC submodules, use data from locally cloned submodules where possible
(cd /var/local/jenkins/grpc-node/ && git submodule foreach 'cd /var/local/git/grpc-node \
&& git submodule update --init --recursive --reference /var/local/jenkins/grpc-node/${name} \
${name}')

# Use the pending c-core changes if possible
if [ -d "/var/local/jenkins/grpc" ]; then
  cd /var/local/jenkins/grpc
  CURRENT_COMMIT="$(git rev-parse --verify HEAD)"
  cd /var/local/git/grpc-node/packages/grpc-native-core/deps/grpc/
  git fetch --tags --progress https://github.com/grpc/grpc.git +refs/pull/*:refs/remotes/origin/pr/*
  git checkout $CURRENT_COMMIT
  git submodule update --init --recursive --reference /var/local/jenkins/grpc
fi

# copy service account keys if available
cp -r /var/local/jenkins/service_account $HOME || true

cd /var/local/git/grpc-node

# build Node interop client & server
npm install -g node-gyp gulp
npm install
gulp setup
