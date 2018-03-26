#!/usr/bin/env bash
# Copyright 2018 gRPC authors.
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

# change to grpc repo root
cd $(dirname $0)/../../..

git submodule update --init

# Build protoc and grpc_cpp_plugin. Codegen is not cross-compiled to Android
make HAS_SYSTEM_PROTOBUF=false

# TODO(ericgribkoff) Remove when this commit (already in master) is included in
# next protobuf release
cd third_party/protobuf
git fetch
git cherry-pick 7daa320065f3bea2b54bf983337d1724f153422d -m 1

cd ../../examples/android/helloworld
./gradlew build \
    -Dprotoc=../../../third_party/protobuf/src/protoc \
    -Dgrpc_cpp_plugin=../../../bins/opt/grpc_cpp_plugin
