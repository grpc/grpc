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

REPO_ROOT="$(pwd)"

git submodule update --init

# Build protoc and grpc_cpp_plugin. Codegen is not cross-compiled to Android
make HAS_SYSTEM_PROTOBUF=false

# Build and run interop instrumentation tests on Firebase Test Lab

cd "${REPO_ROOT}/src/android/test/interop/"
./gradlew assembleDebug \
    "-Pprotoc=${REPO_ROOT}/third_party/protobuf/src/protoc" \
    "-Pgrpc_cpp_plugin=${REPO_ROOT}/bins/opt/grpc_cpp_plugin"
./gradlew assembleDebugAndroidTest \
    "-Pprotoc=${REPO_ROOT}/third_party/protobuf/src/protoc" \
    "-Pgrpc_cpp_plugin=${REPO_ROOT}/bins/opt/grpc_cpp_plugin"
gcloud firebase test android run \
    --type instrumentation \
    --app app/build/outputs/apk/debug/app-debug.apk \
    --test app/build/outputs/apk/androidTest/debug/app-debug-androidTest.apk \
    --device model=Nexus6P,version=27,locale=en,orientation=portrait \
    --device model=Nexus6P,version=26,locale=en,orientation=portrait \
    --device model=Nexus6P,version=25,locale=en,orientation=portrait \
    --device model=Nexus6P,version=24,locale=en,orientation=portrait \
    --device model=Nexus6P,version=23,locale=en,orientation=portrait \
    --device model=Nexus6,version=22,locale=en,orientation=portrait \
    --device model=Nexus6,version=21,locale=en,orientation=portrait


# Build hello world example

cd "${REPO_ROOT}/examples/android/helloworld"
./gradlew build \
    "-Pprotoc=${REPO_ROOT}/third_party/protobuf/src/protoc" \
    "-Pgrpc_cpp_plugin=${REPO_ROOT}/bins/opt/grpc_cpp_plugin"
