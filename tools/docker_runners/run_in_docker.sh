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

# See tools/docker_runners/examples for more usage info.

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

# Args on top of what's already set by build_and_run_docker.sh
DOCKER_EXTRA_PRIVILEGED_ARGS=(
  # TODO(jtattermusch): is "--privileged" actually needed for coredumps inside docker containers?
  "--privileged"
  # TODO(jtattermusch): is "--privileged" actually needed for coredumps inside docker containers?
  "--security-opt=seccomp=unconfined"
)
# Args on top of what's already set by build_and_run_docker.sh
DOCKER_EXTRA_NETWORK_ARGS=(
  # Using host network allows using port server running on the host machine (and not just in the docker container)
  "--network=host"
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

# the original DOCKER_EXTRA_ARGS + all the args defined in this script
export DOCKER_EXTRA_ARGS=""${DOCKER_EXTRA_PRIVILEGED_ARGS[@]}" ${DOCKER_EXTRA_NETWORK_ARGS[@]} ${DOCKER_EXTRA_ARGS}"
# download the docker images from dockerhub instead of building them locally
export DOCKERHUB_ORGANIZATION=grpctesting

exec tools/run_tests/dockerize/build_and_run_docker.sh "$@"
