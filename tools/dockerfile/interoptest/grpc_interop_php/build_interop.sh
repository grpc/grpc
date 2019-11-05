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
# Builds PHP interop server and client in a base image.
set -ex

mkdir -p /var/local/git
git clone /var/local/jenkins/grpc /var/local/git/grpc
# clone gRPC submodules, use data from locally cloned submodules where possible
(cd /var/local/jenkins/grpc/ && git submodule foreach 'cd /var/local/git/grpc \
&& git submodule update --init --reference /var/local/jenkins/grpc/${name} \
${name}')

# copy service account keys if available
cp -r /var/local/jenkins/service_account $HOME || true

cd /var/local/git/grpc

# Install gRPC C core and build codegen plugins
make -j4 install_c plugins

(cd src/php/ext/grpc && phpize && ./configure && make -j4)

# Install protobuf (need access to protoc)
(cd third_party/protobuf && make -j4 install)

cd src/php

DONE=0
for ((i = 0; i < 5; i++)); do
  php -d extension=ext/grpc/modules/grpc.so /usr/local/bin/composer install && DONE=1
  [[ "$DONE" == 1 ]] && break
done
[[ "$DONE" != 1 ]] && echo "Failed to do composer install" && exit 1

./bin/generate_proto_php.sh
