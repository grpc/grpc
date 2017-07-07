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

# Builds the gRPC Android instrumented interop tests inside a docker container
# and runs them on Firebase Test Lab

DOCKERFILE=tools/dockerfile/interoptest/grpc_interop_android_java/Dockerfile
DOCKER_TAG=android_interop_test
SERVICE_KEY=~/android-interops-service-key.json
HELPER=$(pwd)/tools/run_tests/interop/android/android_interop_helper.sh

docker build -t $DOCKER_TAG -f $DOCKERFILE .

docker run --interactive --rm \
  --volume="$SERVICE_KEY":/service-key.json:ro \
  --volume="$HELPER":/android_interop_helper.sh:ro \
  $DOCKER_TAG \
      /bin/bash -c "/android_interop_helper.sh /service-key.json"
