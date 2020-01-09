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

cd "$(dirname "$0")/../../.."
grpc_dir=$(pwd)

echo "deb http://archive.debian.org/debian jessie-backports main" | tee /etc/apt/sources.list.d/jessie-backports.list
echo 'Acquire::Check-Valid-Until "false";' > /etc/apt/apt.conf
sed -i '/deb http:\/\/deb.debian.org\/debian jessie-updates main/d' /etc/apt/sources.list
apt-get update
apt-get install -t jessie-backports -y libssl-dev wget

# Install CMake 3.16
wget -q -O cmake-linux.sh https://github.com/Kitware/CMake/releases/download/v3.16.1/cmake-3.16.1-Linux-x86_64.sh
sh cmake-linux.sh -- --skip-license --prefix=/usr
rm cmake-linux.sh

# Build helloworld example.
# This uses CMake's FetchContent module to download gRPC and its dependencies
# and add it to the helloworld project as a subdirectory.
mkdir -p "examples/cpp/helloworld/cmake/build"
pushd "examples/cpp/helloworld/cmake/build"
# We set FETCHCONTENT_SOURCE_DIR_GRPC to use the existing gRPC checkout
# rather than cloning a release.
cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DgRPC_BUILD_TESTS=OFF \
  -DgRPC_SSL_PROVIDER=package \
  -DGRPC_FETCHCONTENT=ON \
  -DFETCHCONTENT_SOURCE_DIR_GRPC="$grpc_dir" \
  ../..
make -j4
popd
