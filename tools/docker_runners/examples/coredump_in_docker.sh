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

# use the docker image used as the default for C++ by run_tests.py
# TODO(jtattermusch): document how to get the right docker image name
# for given run_tests.py --compiler/--arch params.
export DOCKERFILE_DIR=tools/dockerfile/test/cxx_debian9_x64

# add extra docker args if needed
export DOCKER_EXTRA_ARGS=""

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
