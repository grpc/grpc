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
# This script is invoked by run_tests.py to accommodate "test under docker"
# scenario. You should never need to call this script on your own.

# shellcheck disable=SC2103

set -ex

cd "$(dirname "$0")/../../.."
git_root=$(pwd)
cd -

# Inputs
# DOCKERFILE_DIR - Directory in which Dockerfile file is located.
# DOCKER_RUN_SCRIPT - Script to run under docker (relative to grpc repo root)
# DOCKERHUB_ORGANIZATION - If set, pull a prebuilt image from given dockerhub org.

# Use image name based on Dockerfile location checksum
DOCKER_IMAGE_NAME=$(basename "$DOCKERFILE_DIR"):$(sha1sum "$DOCKERFILE_DIR/Dockerfile" | cut -f1 -d\ )

if [ "$DOCKERHUB_ORGANIZATION" != "" ]
then
  DOCKER_IMAGE_NAME=$DOCKERHUB_ORGANIZATION/$DOCKER_IMAGE_NAME
  time docker pull "$DOCKER_IMAGE_NAME"
else
  # Make sure docker image has been built. Should be instantaneous if so.
  docker build -t "$DOCKER_IMAGE_NAME" "$DOCKERFILE_DIR"
fi

# Choose random name for docker container
CONTAINER_NAME="run_tests_$(uuidgen)"

# Git root as seen by the docker instance
docker_instance_git_root=/var/local/jenkins/grpc

# Run tests inside docker
DOCKER_EXIT_CODE=0
# TODO: silence complaint about $TTY_FLAG expansion in some other way
# shellcheck disable=SC2086,SC2154
docker run \
  --cap-add SYS_PTRACE \
  -e "RUN_TESTS_COMMAND=$RUN_TESTS_COMMAND" \
  -e "config=$config" \
  -e "arch=$arch" \
  -e THIS_IS_REALLY_NEEDED='see https://github.com/docker/docker/issues/14203 for why docker is awful' \
  -e HOST_GIT_ROOT="$git_root" \
  -e LOCAL_GIT_ROOT=$docker_instance_git_root \
  -e "BUILD_ID=$BUILD_ID" \
  -e "BUILD_URL=$BUILD_URL" \
  -e "JOB_BASE_NAME=$JOB_BASE_NAME" \
  -e "KOKORO_BUILD_ID=$KOKORO_BUILD_ID" \
  -e "KOKORO_BUILD_NUMBER=$KOKORO_BUILD_NUMBER" \
  -e "KOKORO_BUILD_URL=$KOKORO_BUILD_URL" \
  -e "KOKORO_JOB_NAME=$KOKORO_JOB_NAME" \
  -i \
  $TTY_FLAG \
  --sysctl net.ipv6.conf.all.disable_ipv6=0 \
  -v ~/.config/gcloud:/root/.config/gcloud \
  -v "$git_root:$docker_instance_git_root" \
  -v /tmp/npm-cache:/tmp/npm-cache \
  -w /var/local/git/grpc \
  --name="$CONTAINER_NAME" \
  "$DOCKER_IMAGE_NAME" \
  bash -l "/var/local/jenkins/grpc/$DOCKER_RUN_SCRIPT" || DOCKER_EXIT_CODE=$?

# use unique name for reports.zip to prevent clash between concurrent
# run_tests.py runs 
TEMP_REPORTS_ZIP=$(mktemp)
docker cp "$CONTAINER_NAME:/var/local/git/grpc/reports.zip" "${TEMP_REPORTS_ZIP}" || true
unzip -o "${TEMP_REPORTS_ZIP}" -d "$git_root" || true
rm -f "${TEMP_REPORTS_ZIP}"

# remove the container, possibly killing it first
docker rm -f "$CONTAINER_NAME" || true

exit $DOCKER_EXIT_CODE
