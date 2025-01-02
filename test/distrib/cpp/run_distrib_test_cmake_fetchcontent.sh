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

# Install openssl (to use instead of boringssl)
apt-get update && apt-get install -y libssl-dev

# Use externally provided env to determine build parallelism, otherwise use default.
GRPC_CPP_DISTRIBTEST_BUILD_COMPILER_JOBS=${GRPC_CPP_DISTRIBTEST_BUILD_COMPILER_JOBS:-4}

# Build helloworld example.
# This uses CMake's FetchContent module to download gRPC and its dependencies
# and add it to the helloworld project as a subdirectory.
mkdir -p "examples/cpp/helloworld/cmake/build"
pushd "examples/cpp/helloworld/cmake/build"
# We set FETCHCONTENT_SOURCE_DIR_GRPC to use the existing gRPC checkout
# rather than cloning a release.
cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_STANDARD=17 \
  -DgRPC_BUILD_TESTS=OFF \
  -DgRPC_SSL_PROVIDER=package \
  -DGRPC_FETCHCONTENT=ON \
  -Dprotobuf_INSTALL=OFF \
  -Dutf8_range_ENABLE_INSTALL=OFF \
  -DFETCHCONTENT_SOURCE_DIR_GRPC="$grpc_dir" \
  ../..
make "-j${GRPC_CPP_DISTRIBTEST_BUILD_COMPILER_JOBS}"
popd
