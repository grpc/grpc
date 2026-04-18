#!/bin/bash
# Copyright 2024 gRPC authors.
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

# Build script for standalone gRPC Python protoc plugin

set -ex

cd "$(dirname "$0")/../../.."

mkdir -p cmake/build
pushd cmake/build

# Configure build - use system dependencies where possible to reduce build time
cmake -DgRPC_BUILD_TESTS=OFF \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_CXX_STANDARD=17 \
      -DgRPC_BUILD_GRPC_PYTHON_PLUGIN=ON \
      ../..

# Use externally provided env to determine build parallelism, otherwise use default.
GRPC_PROTOC_BUILD_COMPILER_JOBS=${GRPC_PROTOC_BUILD_COMPILER_JOBS:-4}

# Build only the Python plugin target
make grpc_python_plugin "-j${GRPC_PROTOC_BUILD_COMPILER_JOBS}"

popd

# Create artifacts directory and copy the plugin
mkdir -p "${ARTIFACTS_OUT}"
cp cmake/build/grpc_python_plugin "${ARTIFACTS_OUT}/"

# Create protoc-compatible name
cp "${ARTIFACTS_OUT}/grpc_python_plugin" "${ARTIFACTS_OUT}/protoc-gen-grpc_python"

# Strip binaries to reduce size
if command -v strip &> /dev/null; then
    strip "${ARTIFACTS_OUT}/grpc_python_plugin"
    strip "${ARTIFACTS_OUT}/protoc-gen-grpc_python"
fi

echo "Built gRPC Python plugin successfully:"
ls -la "${ARTIFACTS_OUT}/"