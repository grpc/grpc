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
#
# Builds and runs a fuzzer (specified by the first command line argument)

set -ex

export RUN_COMMAND="tools/fuzzer/build_and_run_fuzzer.sh $1"
export DOCKER_RUN_SCRIPT=tools/run_tests/dockerize/docker_run.sh
export DOCKERFILE_DIR=tools/dockerfile/test/fuzzer
export OUTPUT_DIR=fuzzer_output

runtime=${runtime:-3600}
jobs=${jobs:-3}

tools/run_tests/dockerize/build_and_run_docker.sh \
  -e RUN_COMMAND="$RUN_COMMAND" \
  -e OUTPUT_DIR="$OUTPUT_DIR" \
  -e config="$config" \
  -e runtime="$runtime" \
  -e jobs="$jobs"
