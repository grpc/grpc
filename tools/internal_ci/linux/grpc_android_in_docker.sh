#!/usr/bin/env bash
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

# change to grpc repo root
cd $(dirname $0)/../../..

REPO_ROOT="$(pwd)"

# Build protoc and grpc_cpp_plugin. Codegen is not cross-compiled to Android
mkdir -p cmake/build
pushd cmake/build
cmake -DgRPC_BUILD_TESTS=OFF -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_STANDARD=17 ../..
make protoc grpc_cpp_plugin -j8
popd

PROTOC=${REPO_ROOT}/cmake/build/third_party/protobuf/protoc
PLUGIN=${REPO_ROOT}/cmake/build/grpc_cpp_plugin

# Build and run interop instrumentation tests on Firebase Test Lab
cd "${REPO_ROOT}/src/android/test/interop/"
./gradlew assembleDebug --parallel \
    "-Pprotoc=${PROTOC}" \
    "-Pgrpc_cpp_plugin=${PLUGIN}"
./gradlew assembleDebugAndroidTest \
    "-Pprotoc=${PROTOC}" \
    "-Pgrpc_cpp_plugin=${PLUGIN}"

# Fail after 2 years to force update of Android versions
if [ $(date +%Y%m%d) -ge 20280415 ]; then
  echo "FAILED: Failing test to force an update of the android model and version that are being used every 2 years"
  # The history of this file can be checked to learn how to update the versions
  # Choose an appropriate model and version from this table.
  echo "=== UNIQUE_FIREBASE_MODELS_LIST ==="
  gcloud firebase test android models list
  exit 1
fi

# TODO(jtattermusch): make it easier to run this test locally
# under docker. The issue is that "gcloud firebase" requires being authenticated
# with gcloud first (while on CI we can rely on GCE default credentials).
gcloud firebase test android run \
    --type instrumentation \
    --app app/build/outputs/apk/debug/app-debug.apk \
    --test app/build/outputs/apk/androidTest/debug/app-debug-androidTest.apk \
    --device model=cheetah,version=33,locale=en,orientation=portrait \
    --device model=akita,version=34,locale=en,orientation=portrait \
    --device model=caiman,version=35,locale=en,orientation=portrait

# Build hello world example
cd "${REPO_ROOT}/examples/android/helloworld"
./gradlew build \
    "-Pprotoc=${PROTOC}" \
    "-Pgrpc_cpp_plugin=${PLUGIN}"
