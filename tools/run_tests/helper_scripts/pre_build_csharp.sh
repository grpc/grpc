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

set -ex

# cd to repository root
cd "$(dirname "$0")/../../.."

mkdir -p cmake/build
cd cmake/build

cmake -DgRPC_BUILD_TESTS=OFF -DCMAKE_BUILD_TYPE="${MSBUILD_CONFIG}" ../..

cd ../../src/csharp

dotnet restore Grpc.sln
