#!/bin/bash
# Copyright 2015, gRPC authors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
# Builds C++ interop server and client in a base image.
set -e

mkdir -p /var/local/git
# grpc-java repo
git clone --recursive --depth 1 /var/local/jenkins/grpc-java /var/local/git/grpc-java

# grpc repo (for metrics client and for the stress test wrapper scripts)
git clone /var/local/jenkins/grpc /var/local/git/grpc
# clone gRPC submodules, use data from locally cloned submodules where possible
(cd /var/local/jenkins/grpc/ && git submodule foreach 'cd /var/local/git/grpc \
&& git submodule update --init --reference /var/local/jenkins/grpc/${name} \
${name}')

# Copy service account keys if available
cp -r /var/local/jenkins/service_account $HOME || true

# First build the metrics client in grpc repo
cd /var/local/git/grpc
make metrics_client

# Build all interop test targets (which includes interop server and stress test
# client) in grpc-java repo
cd /var/local/git/grpc-java
./gradlew :grpc-interop-testing:installDist -PskipCodegen=true
