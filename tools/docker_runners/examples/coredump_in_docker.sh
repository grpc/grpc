#!/bin/bash
# Copyright 2021 The gRPC Authors
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
cd "$(dirname "$0")/../../.."

# Use the docker image used as the default for C++ by run_tests.py
# To use the correct docker image for your experiments,
# note that every invocation of run_tests.py with "--use_docker"
# prints the docker image used as a debug message at the end of the run.
# This is expecially important when --compiler/--arch params are
# use, since they usually influence with docker image will be used
# by run_tests.py
export DOCKERFILE_DIR=tools/dockerfile/test/cxx_debian11_x64

# "--privileged" docker arg is required to be able to update /proc/sys/kernel/core_pattern
export DOCKER_EXTRA_ARGS="--privileged"

# start the docker container with interactive shell
tools/docker_runners/run_in_docker.sh bash

# Run these commands under the docker container
#
# Install gdb (or similar command for non-debian based distros)
# $ apt-get update && apt-get install -y gdb
#
# No limit for coredump size
# $ ulimit -c unlimited
#
# Coredumps will be stored to /tmp/coredumps (inside the docker container)
# mkdir /tmp/coredumps
# echo "/tmp/coredumps/core.%p" | tee /proc/sys/kernel/core_pattern
#
# Build e.g. the C tests
# $ ./tools/run_tests/run_tests.py -l c -c dbg --build_only
#
# Run a test that crashes, it will create a coredump.
# $ cmake/build/foo_bar_test
#
# Open coredump under gdb
# $ gdb cmake/build/foo_bar_test /tmp/coredumps/core.XYZ
