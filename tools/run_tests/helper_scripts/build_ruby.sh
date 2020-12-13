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

export GRPC_CONFIG=${CONFIG:-opt}
if [ "${GRPC_CONFIG}" == "dbg" ]
then
  CMAKE_CONFIG=Debug
else
  CMAKE_CONFIG=Release
fi

# change to grpc's ruby directory
cd "$(dirname "$0")/../../.."

rm -rf ./tmp
rake compile

# build grpc_ruby_plugin
mkdir -p cmake/build
pushd cmake/build
cmake -DgRPC_BUILD_TESTS=OFF -DCMAKE_BUILD_TYPE=${CMAKE_CONFIG} ../..
make protoc grpc_ruby_plugin -j2
popd

# unbreak subsequent make builds by restoring zconf.h (previously renamed by cmake build)
# see https://github.com/madler/zlib/issues/133
(cd third_party/zlib; git checkout zconf.h)
