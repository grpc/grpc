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

# Install openssl (to use instead of boringssl)
apt-get update && apt-get install -y libssl-dev

# Install absl
mkdir -p "third_party/abseil-cpp/cmake/build"
pushd "third_party/abseil-cpp/cmake/build"
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_POSITION_INDEPENDENT_CODE=TRUE ../..
make -j4 install
popd

# Install c-ares
# If the distribution provides a new-enough version of c-ares,
# this section can be replaced with:
# apt-get install -y libc-ares-dev
mkdir -p "third_party/cares/cares/cmake/build"
pushd "third_party/cares/cares/cmake/build"
cmake -DCMAKE_BUILD_TYPE=Release ../..
make -j4 install
popd

# Install protobuf
mkdir -p "third_party/protobuf/cmake/build"
pushd "third_party/protobuf/cmake/build"
cmake -Dprotobuf_BUILD_TESTS=OFF -DCMAKE_BUILD_TYPE=Release ..
make -j4 install
popd

# Install re2
mkdir -p "third_party/re2/cmake/build"
pushd "third_party/re2/cmake/build"
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_POSITION_INDEPENDENT_CODE=TRUE ../..
make -j4 install
popd

# Install zlib
mkdir -p "third_party/zlib/cmake/build"
pushd "third_party/zlib/cmake/build"
cmake -DCMAKE_BUILD_TYPE=Release ../..
make -j4 install
popd

# Just before installing gRPC, wipe out contents of all the submodules to simulate
# a standalone build from an archive
# shellcheck disable=SC2016
git submodule foreach 'cd $toplevel; rm -rf $name'

# Install gRPC
mkdir -p "cmake/build"
pushd "cmake/build"
cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DgRPC_INSTALL=ON \
  -DgRPC_BUILD_TESTS=OFF \
  -DgRPC_CARES_PROVIDER=package \
  -DgRPC_ABSL_PROVIDER=package \
  -DgRPC_PROTOBUF_PROVIDER=package \
  -DgRPC_RE2_PROVIDER=package \
  -DgRPC_SSL_PROVIDER=package \
  -DgRPC_ZLIB_PROVIDER=package \
  ../..
make -j4 install
popd

# Build helloworld example using cmake
mkdir -p "examples/cpp/helloworld/cmake/build"
pushd "examples/cpp/helloworld/cmake/build"
cmake ../..
make
popd
