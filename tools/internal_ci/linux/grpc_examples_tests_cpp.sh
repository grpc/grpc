#!/usr/bin/env bash
# Copyright 2025 gRPC authors.
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

source tools/internal_ci/helper_scripts/prepare_build_linux_rc
# install pre-requisites for gRPC C core build
sudo apt update
sudo apt install -y build-essential autoconf libtool pkg-config cmake python3 python3-pip clang


export DOCKERFILE_DIR=tools/dockerfile/test/bazel
export DOCKER_RUN_SCRIPT=tools/internal_ci/linux/grpc_examples_tests_cpp_in_docker.sh
#exec tools/run_tests/dockerize/build_and_run_docker.sh


# For linux we use secure_getenv to read env variable
# this return null if linux capabilites are added on the executable
# For customer using linux capability , they need to define "--define GRPC_FORCE_UNSECURE_GETENV=1"
# to read env variables.
# enable log

tools/bazel version

# Build without the define , this will use secure_getenv
tools/bazel build  //examples/cpp/helloworld:greeter_callback_client
# Add linux capability
setcap "cap_net_raw=ep" ./bazel-bin/examples/cpp/helloworld/greeter_callback_client
getcap ./bazel-bin/examples/cpp/helloworld/greeter_callback_client
export GRPC_TRACE=http
./bazel-bin/examples/cpp/helloworld/greeter_callback_client &> wow.log
cat wow.log
# We should not see log get enabled as secure_getenv will return null in this case
grep "gRPC Tracers:" wow.log
return_code=$?
if [[ $return_code -eq 0 ]]; then
    fail "Able to read env variable with linux capability set"
fi

# Build using the define to force "getenv" instead of "secure_getenv"
tools/bazel build  --define GRPC_FORCE_UNSECURE_GETENV=1 //examples/cpp/helloworld:greeter_callback_client
# Add linux capability
setcap "cap_net_raw=ep" ./bazel-bin/examples/cpp/helloworld/greeter_callback_client
ls -lrt ./bazel-bin/examples/cpp/helloworld/greeter_callback_client
./bazel-bin/examples/cpp/helloworld/greeter_callback_client &> wow.log
grep "gRPC Tracers:" wow.log
return_code=$?
# check if logs got enabled
if [[ ! $return_code -eq 0 ]]; then
    fail "Fail to read env variable with linux capability"
fi
