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

# Use externally provided env to determine build parallelism, otherwise use default.
GRPC_CPP_DISTRIBTEST_BUILD_COMPILER_JOBS=${GRPC_CPP_DISTRIBTEST_BUILD_COMPILER_JOBS:-4}

# Install absl
mkdir -p "third_party/abseil-cpp/cmake/build"
pushd "third_party/abseil-cpp/cmake/build"
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_POSITION_INDEPENDENT_CODE=TRUE ../..
make "-j${GRPC_CPP_DISTRIBTEST_BUILD_COMPILER_JOBS}" install
popd

# Install c-ares
mkdir -p "third_party/cares/cares/cmake/build"
pushd "third_party/cares/cares/cmake/build"
cmake -DCMAKE_BUILD_TYPE=Release ../..
make "-j${GRPC_CPP_DISTRIBTEST_BUILD_COMPILER_JOBS}" install
popd

# Install protobuf
mkdir -p "third_party/protobuf/cmake/build"
pushd "third_party/protobuf/cmake/build"
cmake -Dprotobuf_BUILD_TESTS=OFF -DCMAKE_BUILD_TYPE=Release -Dprotobuf_ABSL_PROVIDER=package ../..
make "-j${GRPC_CPP_DISTRIBTEST_BUILD_COMPILER_JOBS}" install
popd

# Install re2 pkg-config using `make install`
# because its `cmake install` doesn't install it
# https://github.com/google/re2/issues/399
pushd "third_party/re2"
make "-j${GRPC_CPP_DISTRIBTEST_BUILD_COMPILER_JOBS}" install
popd

# Install re2
mkdir -p "third_party/re2/cmake/build"
pushd "third_party/re2/cmake/build"
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_POSITION_INDEPENDENT_CODE=TRUE ../..
make "-j${GRPC_CPP_DISTRIBTEST_BUILD_COMPILER_JOBS}" install
popd

# Install zlib
mkdir -p "third_party/zlib/cmake/build"
pushd "third_party/zlib/cmake/build"
cmake -DCMAKE_BUILD_TYPE=Release ../..
make "-j${GRPC_CPP_DISTRIBTEST_BUILD_COMPILER_JOBS}" install
popd

# Just before installing gRPC, wipe out contents of all the submodules to simulate
# a standalone build from an archive.
# Get list of submodules from the .gitmodules file since for "git submodule foreach"
# we'd need to be in a git workspace (and that's not the case when running
# distribtests as a bazel action)
grep 'path = ' .gitmodules | sed 's/^.*path = //' | xargs rm -rf

# Install gRPC
# TODO(jtattermusch): avoid the need for setting utf8_range_DIR
mkdir -p "cmake/build"
pushd "cmake/build"
cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr/local/grpc \
  -DgRPC_INSTALL=ON \
  -DgRPC_BUILD_TESTS=OFF \
  -DgRPC_ABSL_PROVIDER=package \
  -DgRPC_CARES_PROVIDER=package \
  -DgRPC_PROTOBUF_PROVIDER=package \
  -Dutf8_range_DIR=/usr/local/lib/cmake/utf8_range \
  -DgRPC_RE2_PROVIDER=package \
  -DgRPC_SSL_PROVIDER=package \
  -DgRPC_ZLIB_PROVIDER=package \
  ../..
make "-j${GRPC_CPP_DISTRIBTEST_BUILD_COMPILER_JOBS}" install
popd

# Set pkgconfig envs
export PKG_CONFIG_PATH=/usr/local/grpc/lib/pkgconfig
export PATH=$PATH:/usr/local/grpc/bin

# Show pkg-config configuration
pkg-config --cflags grpc
pkg-config --libs --static grpc
pkg-config --cflags grpc++
pkg-config --libs --static grpc++

# Build helloworld example using Makefile and pkg-config
pushd examples/cpp/helloworld
make "-j${GRPC_CPP_DISTRIBTEST_BUILD_COMPILER_JOBS}"
popd

# Build route_guide example using Makefile and pkg-config
pushd examples/cpp/route_guide
make "-j${GRPC_CPP_DISTRIBTEST_BUILD_COMPILER_JOBS}"
popd
