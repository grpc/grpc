#!/bin/bash
# Copyright 2016 The gRPC Authors
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
# OUTPUT_DIR - Directory (relatively to git repo root) that will be copied from inside docker container after finishing.
# DOCKERHUB_ORGANIZATION - If set, pull a prebuilt image from given dockerhub org.
# $@ - Extra args to pass to the "docker run" command.

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

if [[ -t 0 ]]; then
  DOCKER_TTY_ARGS="-it"
else
  # The input device on kokoro is not a TTY, so -it does not work.
  DOCKER_TTY_ARGS=
fi

# Git root as seen by the docker instance
# TODO(jtattermusch): rename to a more descriptive directory name
# currently that's nontrivial because the name is hardcoded in many places.
EXTERNAL_GIT_ROOT=/var/local/jenkins/grpc

# temporary directory that will be mounted to the docker container
# as a way to persist output files.
# use unique name for the output directory to prevent clash between concurrent
# runs of multiple docker containers
TEMP_REPORT_DIR="$(mktemp -d)"
TEMP_OUTPUT_DIR="$(mktemp -d)"

# Run tests inside docker
DOCKER_EXIT_CODE=0
# TODO: silence complaint about $DOCKER_TTY_ARGS expansion in some other way
# shellcheck disable=SC2086,SC2154
docker run \
  "$@" \
  ${DOCKER_TTY_ARGS} \
  ${EXTRA_DOCKER_ARGS} \
  --cap-add SYS_PTRACE \
  -e "DOCKER_RUN_SCRIPT_COMMAND=${DOCKER_RUN_SCRIPT_COMMAND}" \
  -e "EXTERNAL_GIT_ROOT=${EXTERNAL_GIT_ROOT}" \
  -e "OUTPUT_DIR=${OUTPUT_DIR}" \
  --env-file tools/run_tests/dockerize/docker_propagate_env.list \
  --rm \
  --sysctl net.ipv6.conf.all.disable_ipv6=0 \
  -v "${git_root}:${EXTERNAL_GIT_ROOT}" \
  -v "${TEMP_REPORT_DIR}:/var/local/report_dir" \
  -v "${TEMP_OUTPUT_DIR}:/var/local/output_dir" \
  "${DOCKER_IMAGE_NAME}" \
  bash -l "/var/local/jenkins/grpc/${DOCKER_RUN_SCRIPT}" || DOCKER_EXIT_CODE=$?

# Copy reports stored by the container (if any)
if [ "${GRPC_TEST_REPORT_BASE_DIR}" != "" ]
then
  mkdir -p "${GRPC_TEST_REPORT_BASE_DIR}"
  cp -r "${TEMP_REPORT_DIR}"/* "${GRPC_TEST_REPORT_BASE_DIR}" || true
else
  cp -r "${TEMP_REPORT_DIR}"/* "${git_root}" || true
fi

# Copy contents of OUTPUT_DIR back under the git repo root
if [ "${OUTPUT_DIR}" != "" ]
then
  # create the directory if it doesn't exist yet.
  mkdir -p "${TEMP_OUTPUT_DIR}/${OUTPUT_DIR}"
  cp -r "${TEMP_OUTPUT_DIR}/${OUTPUT_DIR}" "${git_root}" || DOCKER_EXIT_CODE=$?
fi

exit $DOCKER_EXIT_CODE
