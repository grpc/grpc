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

# TODO(jtattermusch): make sure bazel cache is persisted between runs

# Note that the port server must be running so that the bazel tests can pass.
# (Run "tools/run_tests/start_port_server.py" first)

# use the default docker image used for bazel builds
export DOCKERFILE_DIR=tools/dockerfile/test/bazel
# Using host network allows using port server running on the host machine (and not just in the docker container)
# TODO(jtattermusch): interestingly, the bazel build fails when "--privileged=true" docker arg is used (it probably has to do with sandboxing)
export DOCKER_EXTRA_ARGS="--network=host"
tools/docker_runners/run_in_docker.sh bazel test //test/...
