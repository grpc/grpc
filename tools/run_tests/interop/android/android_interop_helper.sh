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

# Helper that runs inside the docker container and builds the APKs and
# invokes Firebase Test Lab via gcloud.

SERVICE_KEY=$1

gcloud auth activate-service-account --key-file=$SERVICE_KEY || exit 1
gcloud config set project grpc-testing || exit 1

rm -rf grpc-java
git clone https://github.com/grpc/grpc-java.git
cd grpc-java
./gradlew install || exit 1
cd android-interop-testing
../gradlew assembleDebug
../gradlew assembleDebugAndroidTest

gcloud firebase test android run \
  --type instrumentation \
  --app app/build/outputs/apk/app-debug.apk \
  --test app/build/outputs/apk/app-debug-androidTest.apk \
  --device model=Nexus6,version=21,locale=en,orientation=portrait
