#!/bin/bash
# Copyright 2015 gRPC authors.
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
# This script is invoked by run_interop_tests.py to build the docker image
# for interop testing. You should never need to call this script on your own.

set -ex

# Params:
#  INTEROP_IMAGE - name of tag of the final interop image
#  BASE_NAME - base name used to locate the base Dockerfile and build script
#  BUILD_INTEROP_DOCKER_EXTRA_ARGS - optional args to be passed to the
#    docker run command
#  GRPC_ROOT - grpc base directory, default to top of this tree.
#  GRPC_GO_ROOT - grpc-go base directory, default to '$GRPC_ROOT/../grpc-go'
#  GRPC_JAVA_ROOT - grpc-java base directory, default to '$GRPC_ROOT/../grpc-java'

cd "$(dirname "$0")/../../.."
echo "GRPC_ROOT: ${GRPC_ROOT:=$(pwd)}"
MOUNT_ARGS="-v $GRPC_ROOT:/var/local/jenkins/grpc:ro"

echo "GRPC_JAVA_ROOT: ${GRPC_JAVA_ROOT:=$(cd ../grpc-java && pwd)}"
if [ -n "$GRPC_JAVA_ROOT" ]
then
  MOUNT_ARGS+=" -v $GRPC_JAVA_ROOT:/var/local/jenkins/grpc-java:ro"
else
  echo "WARNING: grpc-java not found, it won't be mounted to the docker container."
fi

echo "GRPC_GO_ROOT: ${GRPC_GO_ROOT:=$(cd ../grpc-go && pwd)}"
if [ -n "$GRPC_GO_ROOT" ]
then
  MOUNT_ARGS+=" -v $GRPC_GO_ROOT:/var/local/jenkins/grpc-go:ro"
else
  echo "WARNING: grpc-go not found, it won't be mounted to the docker container."
fi

echo "GRPC_DART_ROOT: ${GRPC_DART_ROOT:=$(cd ../grpc-dart && pwd)}"
if [ -n "$GRPC_DART_ROOT" ]
then
  MOUNT_ARGS+=" -v $GRPC_DART_ROOT:/var/local/jenkins/grpc-dart:ro"
else
  echo "WARNING: grpc-dart not found, it won't be mounted to the docker container."
fi

echo "GRPC_NODE_ROOT: ${GRPC_NODE_ROOT:=$(cd ../grpc-node && pwd)}"
if [ -n "$GRPC_NODE_ROOT" ]
then
  MOUNT_ARGS+=" -v $GRPC_NODE_ROOT:/var/local/jenkins/grpc-node:ro"
else
  echo "WARNING: grpc-node not found, it won't be mounted to the docker container."
fi

echo "GRPC_DOTNET_ROOT: ${GRPC_DOTNET_ROOT:=$(cd ../grpc-dotnet && pwd)}"
if [ -n "$GRPC_DOTNET_ROOT" ]
then
  MOUNT_ARGS+=" -v $GRPC_DOTNET_ROOT:/var/local/jenkins/grpc-dotnet:ro"
else
  echo "WARNING: grpc-dotnet not found, it won't be mounted to the docker container."
fi

# Mount service account dir if available.
# If service_directory does not contain the service account JSON file,
# some of the tests will fail.
if [ -e "$HOME/service_account" ]
then
  MOUNT_ARGS+=" -v $HOME/service_account:/var/local/jenkins/service_account:ro"
fi

BASE_IMAGE_DIR="tools/dockerfile/interoptest/$BASE_NAME"
# The exact base docker image to use and its version is determined by the corresponding .current_version file
BASE_IMAGE="$(cat "${BASE_IMAGE_DIR}.current_version")"

# If TTY is available, the running container can be conveniently terminated with Ctrl+C.
if [[ -t 0 ]]; then
  DOCKER_TTY_ARGS=("-it")
else
  # The input device on kokoro is not a TTY, so -it does not work.
  DOCKER_TTY_ARGS=()
fi

CONTAINER_NAME="build_${BASE_NAME}_$(uuidgen)"

# Prepare image for interop tests, commit it on success.
# TODO: Figure out if is safe to eliminate the suppression. It's currently here
# because $MOUNT_ARGS and $BUILD_INTEROP_DOCKER_EXTRA_ARGS can have legitimate
# spaces, but the "correct" way to do this is to utilize proper arrays.
# shellcheck disable=SC2086
(docker run \
  --cap-add SYS_PTRACE \
  --env-file "tools/run_tests/dockerize/docker_propagate_env.list" \
  "${DOCKER_TTY_ARGS[@]}" \
  $MOUNT_ARGS \
  $BUILD_INTEROP_DOCKER_EXTRA_ARGS \
  --name="$CONTAINER_NAME" \
  "$BASE_IMAGE" \
  bash -l "/var/local/jenkins/grpc/tools/dockerfile/interoptest/$BASE_NAME/build_interop.sh" \
  && docker commit "$CONTAINER_NAME" "$INTEROP_IMAGE" \
  && echo "Successfully built image $INTEROP_IMAGE")
EXITCODE=$?

# remove intermediate container, possibly killing it first
docker rm -f "$CONTAINER_NAME"

exit $EXITCODE
