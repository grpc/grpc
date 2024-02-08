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
# Builds C++ interop server and client in a base image.
set -ex

mkdir -p /var/local/git
echo "DO NOT SUBMIT cloning: $(date)"
git clone /var/local/jenkins/grpc /var/local/git/grpc
echo "DO NOT SUBMIT updating submodules: $(date)"
# clone gRPC submodules, use data from locally cloned submodules where possible
(cd /var/local/jenkins/grpc/ && git submodule foreach 'cd /var/local/git/grpc \
&& git submodule update --init --reference /var/local/jenkins/grpc/${name} \
${name}')

# copy service account keys if available
cp -r /var/local/jenkins/service_account $HOME || true

cd /var/local/git/grpc

grep -R 'DO NOT SUBMIT'

# Install the roots.pem
mkdir -p /usr/local/share/grpc
cp etc/roots.pem /usr/local/share/grpc/roots.pem

# build C++ interop client, interop server and http2 interop client
mkdir -p cmake/build
cd cmake/build
echo "DO NOT SUBMIT make: $(date)"
N_JOBS=$(nproc --all)
cmake -DgRPC_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release ../..
make interop_client interop_server -j${N_JOBS}
make http2_client -j${N_JOBS}
echo "DO NOT SUBMIT build_interop.sh done: $(date)"
