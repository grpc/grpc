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
EXTERNAL_GIT_ROOT=/var/local/jenkins/grpc

# temporary directory that will be mounted to the docker container
# as a way to persist output files.
# use unique name for the output directory to prevent clash between concurrent
# runs of multiple docker containers
TEMP_OUTPUT_DIR="$(mktemp -d)"

# Run tests inside docker
DOCKER_EXIT_CODE=0
# TODO: silence complaint about $DOCKER_TTY_ARGS expansion in some other way
# shellcheck disable=SC2086,SC2154
docker run \
  "$@" \
  ${DOCKER_TTY_ARGS} \
  --cap-add SYS_PTRACE \
  -e "RUN_TESTS_COMMAND=${RUN_TESTS_COMMAND}" \
  -e "EXTERNAL_GIT_ROOT=${EXTERNAL_GIT_ROOT}" \
  --env-file tools/run_tests/dockerize/docker_propagate_env.list \
  --rm \
  --sysctl net.ipv6.conf.all.disable_ipv6=0 \
  -v "${git_root}:${EXTERNAL_GIT_ROOT}" \
  -v "${TEMP_OUTPUT_DIR}:/var/local/output_dir" \
  -w /var/local/git/grpc \
  "${DOCKER_IMAGE_NAME}" \
  bash -l "/var/local/jenkins/grpc/${DOCKER_RUN_SCRIPT}" || DOCKER_EXIT_CODE=$?

if [ "${GRPC_TEST_REPORT_BASE_DIR}" != "" ]
then
  REPORTS_DEST_DIR="${GRPC_TEST_REPORT_BASE_DIR}"
else
  REPORTS_DEST_DIR="${git_root}"
fi

# reports.zip will be stored by the container after run_tests.py has finished.
TEMP_REPORTS_ZIP="${TEMP_OUTPUT_DIR}/reports.zip"
unzip -o "${TEMP_REPORTS_ZIP}" -d "${REPORTS_DEST_DIR}" || true
rm -f "${TEMP_REPORTS_ZIP}"

exit $DOCKER_EXIT_CODE
