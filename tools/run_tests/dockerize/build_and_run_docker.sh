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

set -ex

cd $(dirname $0)/../../..
git_root=$(pwd)
cd -

# Inputs
# DOCKERFILE_DIR - Directory in which Dockerfile file is located.
# DOCKER_RUN_SCRIPT - Script to run under docker (relative to grpc repo root)
# OUTPUT_DIR - Directory that will be copied from inside docker after finishing.
# DOCKERHUB_ORGANIZATION - If set, pull a prebuilt image from given dockerhub org.
# DOCKER_BASE_IMAGE - If set, pull the latest base image.
# $@ - Extra args to pass to docker run

# Use image name based on Dockerfile location checksum
DOCKER_IMAGE_NAME=$(basename $DOCKERFILE_DIR)_$(sha1sum $DOCKERFILE_DIR/Dockerfile | cut -f1 -d\ )

# Pull the base image to force an update
if [ "$DOCKER_BASE_IMAGE" != "" ]
then
  time docker pull $DOCKER_BASE_IMAGE
fi

if [ "$DOCKERHUB_ORGANIZATION" != "" ]
then
  DOCKER_IMAGE_NAME=$DOCKERHUB_ORGANIZATION/$DOCKER_IMAGE_NAME
  time docker pull $DOCKER_IMAGE_NAME
else
  # Make sure docker image has been built. Should be instantaneous if so.
  docker build -t $DOCKER_IMAGE_NAME $DOCKERFILE_DIR
fi

# Choose random name for docker container
CONTAINER_NAME="build_and_run_docker_$(uuidgen)"

# Run command inside docker
docker run \
  "$@" \
  -e EXTERNAL_GIT_ROOT="/var/local/jenkins/grpc" \
  -e THIS_IS_REALLY_NEEDED='see https://github.com/docker/docker/issues/14203 for why docker is awful' \
  -v "$git_root:/var/local/jenkins/grpc:ro" \
  -w /var/local/git/grpc \
  --name=$CONTAINER_NAME \
  $EXTRA_DOCKER_ARGS \
  $DOCKER_IMAGE_NAME \
  /bin/bash -l "/var/local/jenkins/grpc/$DOCKER_RUN_SCRIPT" || FAILED="true"

# Copy output artifacts
if [ "$OUTPUT_DIR" != "" ]
then
  docker cp "$CONTAINER_NAME:/var/local/git/grpc/$OUTPUT_DIR" "$git_root" || FAILED="true"
fi

# remove the container, possibly killing it first
docker rm -f $CONTAINER_NAME || true

if [ "$FAILED" != "" ]
then
  exit 1
fi
