#!/bin/bash
# Copyright 2022 The gRPC Authors
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

# Set install path to avoid installing to system paths
cd "$(dirname "$0")/../../.."
mkdir -p cmake/install
INSTALL_PATH="$(pwd)/cmake/install"

# Install abseil-cpp since opentelemetry CMake uses find_package to find it.
cd third_party/abseil-cpp
mkdir build
cd build
cmake -DCMAKE_CXX_STANDARD=14 -DABSL_BUILD_TESTING=OFF -DCMAKE_BUILD_TYPE="${MSBUILD_CONFIG}" -DCMAKE_INSTALL_PREFIX="${INSTALL_PATH}" "$@" ..
make -j"${GRPC_RUN_TESTS_JOBS}" install

# Install opentelemetry-cpp since we only support "package" mode for opentelemetry at present.
cd ../../..
cd third_party/opentelemetry-cpp
mkdir build
cd build
cmake -DCMAKE_CXX_STANDARD=14 -DWITH_ABSEIL=ON -DBUILD_TESTING=OFF -DWITH_BENCHMARK=OFF -DCMAKE_BUILD_TYPE="${MSBUILD_CONFIG}" -DCMAKE_INSTALL_PREFIX="${INSTALL_PATH}" "$@" ..
make -j"${GRPC_RUN_TESTS_JOBS}" install

cd ../../..
mkdir -p cmake/build
cd cmake/build

# TODO(yashykt/veblush): Remove workaround after fixing b/332425004 
if [ "${GRPC_RUNTESTS_ARCHITECTURE}" = "x86" ]; then
cmake -DCMAKE_CXX_STANDARD=14 -DgRPC_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE="${MSBUILD_CONFIG}" "$@" ../..
else
cmake -DCMAKE_CXX_STANDARD=14 -DgRPC_BUILD_GRPCPP_OTEL_PLUGIN=ON -DgRPC_ABSL_PROVIDER=package -DgRPC_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE="${MSBUILD_CONFIG}" -DCMAKE_INSTALL_PREFIX="${INSTALL_PATH}" "$@" ../..
fi

# GRPC_RUN_TESTS_CXX_LANGUAGE_SUFFIX will be set to either "c" or "cxx"
make -j"${GRPC_RUN_TESTS_JOBS}" "buildtests_${GRPC_RUN_TESTS_CXX_LANGUAGE_SUFFIX}" "tools_${GRPC_RUN_TESTS_CXX_LANGUAGE_SUFFIX}"
