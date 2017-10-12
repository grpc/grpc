#!/bin/bash
# Copyright 2017 gRPC authors.
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

set -ex

cd $(dirname $0)/../../..

echo "deb http://ftp.debian.org/debian jessie-backports main" | tee /etc/apt/sources.list.d/jessie-backports.list
apt-get update
#apt-get install -t jessie-backports -y libc-ares-dev  # we need specifically version 1.12
apt-get install -t jessie-backports -y libssl-dev

# Install c-ares
cd third_party/cares/cares
git fetch origin
git checkout cares-1_13_0
mkdir -p cmake/build
cd cmake/build
cmake -DCMAKE_BUILD_TYPE=Release ../..
make -j4 install
cd ../../../../..
rm -rf third_party/cares/cares  # wipe out to prevent influencing the grpc build

# Install zlib
cd third_party/zlib
mkdir -p cmake/build
cd cmake/build
cmake -DCMAKE_BUILD_TYPE=Release ../..
make -j4 install
cd ../../../..
rm -rf third_party/zlib  # wipe out to prevent influencing the grpc build

# Install protobuf
cd third_party/protobuf
mkdir -p cmake/build
cd cmake/build
cmake -Dprotobuf_BUILD_TESTS=OFF -DCMAKE_BUILD_TYPE=Release ..
make -j4 install
cd ../../../..
rm -rf third_party/protobuf  # wipe out to prevent influencing the grpc build

# Install gRPC
mkdir -p cmake/build
cd cmake/build
cmake -DgRPC_INSTALL=ON -DgRPC_BUILD_TESTS=OFF -DgRPC_PROTOBUF_PROVIDER=package -DgRPC_ZLIB_PROVIDER=package -DgRPC_CARES_PROVIDER=package -DgRPC_SSL_PROVIDER=package -DCMAKE_BUILD_TYPE=Release ../..
make -j4 install
cd ../..

# Build helloworld example using cmake
cd examples/cpp/helloworld
mkdir -p cmake/build
cd cmake/build
cmake ../..
make

