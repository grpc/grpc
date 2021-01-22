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
# Builds docker image and runs a command under it.
# You should never need to call this script on your own.

# shellcheck disable=SC2103

set -e

readonly git_root="$(dirname "$(readlink -f "$0")")/../../.."

# Inputs
# DOCKERFILE_DIR - Directory in which Dockerfile file is located.
# DOCKERHUB_ORGANIZATION - dockerhub organization to pull the image from if not available locally.
# $* - Command to run under docker

DOCKER_IMAGE_NAME=$DOCKERHUB_ORGANIZATION/$(basename "$DOCKERFILE_DIR"):$(sha1sum "$DOCKERFILE_DIR/Dockerfile" | cut -f1 -d\ )

if [[ -t 0 ]]; then
  DOCKER_TTY_ARGS="-it"
else
  # The input device on kokoro is not a TTY, so -it does not work.
  DOCKER_TTY_ARGS=
fi

# Run command inside docker
# We assume the docker image has an entrypoint that will run the command under current user's UID and GID.
# That's also why we can mount the workspace as a writable volume (without risk of ending up with files
# in workspace owned by root).
# To make sure the nonroot_entrypoint.sh really exists, we specify it on the command line explicitly.
# shellcheck disable=SC2086
exec docker run \
  $DOCKER_TTY_ARGS \
  --rm=true \
  -v "$git_root:/workspace" \
  -w "/workspace" \
  -e BUILDER_UID="$(id -u)" -e BUILDER_GID="$(id -g)" -e BUILDER_USER="$(id -un)" -e BUILDER_GROUP="$(id -gn)" \
  --entrypoint "/nonroot_entrypoint.sh" \
  "$DOCKER_IMAGE_NAME" \
  bash -c "$*"
