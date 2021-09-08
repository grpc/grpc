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

# Runs C# build in a docker container, but using the local workspace.
# Example usage:
# src/csharp/run_in_docker.sh tools/run_tests/run_tests.py -l csharp

set -ex

readonly grpc_rootdir="$(dirname "$(readlink -f "$0")")/../.."
cd ${grpc_rootdir}

if [[ -t 0 ]]; then
  DOCKER_TTY_ARGS="-it"
else
  # The input device on kokoro is not a TTY, so -it does not work.
  DOCKER_TTY_ARGS=
fi

# use the default C# image
DOCKERFILE_DIR=tools/dockerfile/test/csharp_buster_x64
# Use image name based on Dockerfile location checksum
DOCKER_IMAGE=grpctesting/$(basename "$DOCKERFILE_DIR"):$(sha1sum "$DOCKERFILE_DIR/Dockerfile" | cut -f1 -d\ )

# Run command inside C# docker container.
# the "--network=host" is required for the port server to work correctly.
# TODO(jtattermusch): make sure the docker container doesn't run as root (which pollutes the workspace with files owned by root)
exec docker run $DOCKER_TTY_ARGS --network=host --rm=true -v "${grpc_rootdir}":/workspace -w /workspace $DOCKER_IMAGE bash -c "$*"
