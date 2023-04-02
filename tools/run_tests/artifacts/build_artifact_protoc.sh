#!/bin/bash
# Copyright 2016 gRPC authors.
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

mkdir -p cmake/build
pushd cmake/build

cmake -DgRPC_BUILD_TESTS=OFF -DCMAKE_BUILD_TYPE=Release ../..

# Use externally provided env to determine build parallelism, otherwise use default.
GRPC_PROTOC_BUILD_COMPILER_JOBS=${GRPC_PROTOC_BUILD_COMPILER_JOBS:-2}

make protoc plugins "-j${GRPC_PROTOC_BUILD_COMPILER_JOBS}"

popd

mkdir -p "${ARTIFACTS_OUT}"
cp cmake/build/third_party/protobuf/protoc cmake/build/*_plugin "${ARTIFACTS_OUT}"/
