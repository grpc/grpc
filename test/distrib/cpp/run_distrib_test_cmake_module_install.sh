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

PS4='+ $(date "+[%H:%M:%S %Z]")\011 '
set -ex

cd "$(dirname "$0")/../../.."

# Install openssl (to use instead of boringssl) and wget
apt-get update && apt-get install -y libssl-dev wget

# Install a newer CMake version at runtime to satisfy BoringSSL's 3.22+ requirement
# (The pinned Docker image might only have an older CMake version, e.g. 3.18 on Debian 11)
wget -qO- https://github.com/Kitware/CMake/releases/download/v3.28.1/cmake-3.28.1-linux-x86_64.tar.gz | tar --strip-components=1 -xz -C /usr/local

# Use externally provided env to determine build parallelism, otherwise use default.
GRPC_CPP_DISTRIBTEST_BUILD_COMPILER_JOBS=${GRPC_CPP_DISTRIBTEST_BUILD_COMPILER_JOBS:-4}

# Install gRPC and its dependencies
mkdir -p "cmake/build"
pushd "cmake/build"
cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_STANDARD=17 \
  -DgRPC_INSTALL=ON \
  -DgRPC_BUILD_TESTS=OFF \
  -DgRPC_SSL_PROVIDER=package \
  ../..
make "-j${GRPC_CPP_DISTRIBTEST_BUILD_COMPILER_JOBS}" install
popd

# Build helloworld example using cmake
mkdir -p "examples/cpp/helloworld/cmake/build"
pushd "examples/cpp/helloworld/cmake/build"
cmake -DCMAKE_CXX_STANDARD=17 ../..
make "-j${GRPC_CPP_DISTRIBTEST_BUILD_COMPILER_JOBS}"
popd
