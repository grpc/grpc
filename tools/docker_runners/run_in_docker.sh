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

set -e

# Environment variable used as inputs:
# DOCKERFILE_DIR - Directory in which Dockerfile file is located.
# DOCKER_EXTRA_ARGS - Extra arguments to pass to the "docker run" command.

readonly grpc_rootdir="$(dirname "$(readlink -f "$0")")/../.."
cd ${grpc_rootdir}

if [ "${DOCKERFILE_DIR}" == "" ]
then
    echo "You need to specify the docker image to use by setting DOCKERFILE_DIR env variable."
    echo "See docker image definitions under tools/dockerfile."
    echo ""
    echo "You likely want to set DOCKERFILE_DIR to one of these values:"
    find tools/dockerfile/test -name Dockerfile | xargs -n1 dirname
    exit 1
fi

# Use image name based on Dockerfile location checksum
# For simplicity, currently only testing docker images that have already been pushed
# to dockerhub are supported (see tools/dockerfile/push_testing_images.sh)
# TODO(jtattermusch): add support for building dockerimages locally.
DOCKER_IMAGE=grpctesting/$(basename "$DOCKERFILE_DIR"):$(sha1sum "$DOCKERFILE_DIR/Dockerfile" | cut -f1 -d\ )

# TODO: support building dockerimage locally / pulling it from dockerhub

# If TTY is available, the running container can be conveniently terminated with Ctrl+C.
if [[ -t 0 ]]; then
  DOCKER_TTY_ARGS=("-it")
else
  # The input device on kokoro is not a TTY, so -it does not work.
  DOCKER_TTY_ARGS=()
fi

# args required to be able to run gdb/strace under the docker container
DOCKER_PRIVILEGED_ARGS=(
  "--privileged"
  "--cap-add=SYS_PTRACE"
  "--security-opt=seccomp=unconfined"
)

DOCKER_NETWORK_ARGS=(
  # enable IPv6
  "--sysctl=net.ipv6.conf.all.disable_ipv6=0"
  # use host network, required for the port server to work correctly
  "--network=host"
)

DOCKER_CLEANUP_ARGS=(
  # delete the container when the containers exits
  # (otherwise the container will not release the disk space it used)
  "--rm=true"
)

DOCKER_PROPAGATE_ENV_ARGS=(
  "--env-file=tools/run_tests/dockerize/docker_propagate_env.list" \
)

# Uncomment to run the docker container as current user's UID and GID.
# That way, the files written by the container won't be owned by root (=you won't end up with polluted workspace),
# but it can have some other disadvantages. E.g.:
# - you won't be able install stuff inside the container
# - the home directory inside the container will be broken (you won't be able to write in it).
#   That may actually break some language runtimes completely (e.g. grpc python might not build)
# DOCKER_NONROOT_ARGS=(
#   # run under current user's UID and GID
#   "--user=$(id -u):$(id -g)"
# )

# Enable command echo just before running the final docker command to make the docker args visible.
set -ex

# Run command inside C# docker container.
# - the local clone of grpc repository will be mounted as /workspace.
exec docker run "${DOCKER_TTY_ARGS[@]}" "${DOCKER_PRIVILEGED_ARGS[@]}" "${DOCKER_NETWORK_ARGS[@]}" "${DOCKER_CLEANUP_ARGS[@]}" "${DOCKER_PROPAGATE_ENV_ARGS[@]}" ${DOCKER_EXTRA_ARGS} -v "${grpc_rootdir}":/workspace -w /workspace "${DOCKER_IMAGE}" bash -c "$*"
