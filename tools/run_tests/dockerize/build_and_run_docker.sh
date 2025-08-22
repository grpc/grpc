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
# OUTPUT_DIR - (optional) Directory under the git repo root that will be copied from inside docker container after finishing.
# DOCKER_RUN_SCRIPT - (optional) Script to run under docker (relative to grpc repo root). If specified, the cmdline args
#     passed on the commadline (a.k.a. $@) will be interpreted as extra args to pass to the "docker run" command.
#     If DOCKER_RUN_SCRIPT is not set, $@ will be interpreted as the command and args to run under the docker container.

# The exact docker image to use and its version is determined by the corresponding .current_version file
DOCKER_IMAGE_NAME="$(cat "${DOCKERFILE_DIR}.current_version")"

# If TTY is available, the running container can be conveniently terminated with Ctrl+C.
if [[ -t 0 ]]; then
  DOCKER_TTY_ARGS=("-it")
else
  # The input device on kokoro is not a TTY, so -it does not work.
  DOCKER_TTY_ARGS=()
fi

if [ "${DOCKER_RUN_SCRIPT}" != "" ]
then
  # Execute a well-known script inside the docker container.
  # The script will act as a "wrapper" for the actual test command we want to run.
  # TODO(jtattermusch): is the -l flag still necessary?
  DOCKER_CMD_AND_ARGS=( bash -l "/var/local/jenkins/grpc/${DOCKER_RUN_SCRIPT}" )
  DOCKER_RUN_SCRIPT_ARGS=(
    # propagate the variable with command to be run by the DOCKER_RUN_SCRIPT
    "-e=DOCKER_RUN_SCRIPT_COMMAND=${DOCKER_RUN_SCRIPT_COMMAND}"
    # Interpret any cmdline args as extra docker args.
    # TODO(jtattermusch): remove this hack once there are no places where build_and_run_docker.sh is invoked
    # with args.
    "$@"
  )
else
  # Interpret any cmdline args as the command to be run inside the docker container.
  DOCKER_CMD_AND_ARGS=( "$@" )
  DOCKER_RUN_SCRIPT_ARGS=(
    # TODO(jtattermusch): remove the hack
    "-w=/var/local/jenkins/grpc"
  )
fi

# If available, make KOKORO_KEYSTORE_DIR accessible from the container (as readonly)
if [ "${KOKORO_KEYSTORE_DIR}" != "" ]
then
  MOUNT_KEYSTORE_DIR_ARGS=(
    "-v=${KOKORO_KEYSTORE_DIR}:/kokoro_keystore:ro"
    "-e=KOKORO_KEYSTORE_DIR=/kokoro_keystore"
  )
else
  MOUNT_KEYSTORE_DIR_ARGS=()
fi

# If available, make KOKORO_GFILE_DIR accessible from the container (as readonly)
if [ "${KOKORO_GFILE_DIR}" != "" ]
then
  MOUNT_GFILE_DIR_ARGS=(
    "-v=${KOKORO_GFILE_DIR}:/kokoro_gfile:ro"
    "-e=KOKORO_GFILE_DIR=/kokoro_gfile"
  )
else
  MOUNT_GFILE_DIR_ARGS=()
fi

# If available, make KOKORO_ARTIFACTS_DIR accessible from the container
if [ "${KOKORO_ARTIFACTS_DIR}" != "" ]
then
  MOUNT_ARTIFACTS_DIR_ARGS=(
    "-v=${KOKORO_ARTIFACTS_DIR}:/kokoro_artifacts"
    "-e=KOKORO_ARTIFACTS_DIR=/kokoro_artifacts"
  )
else
  MOUNT_ARTIFACTS_DIR_ARGS=()
fi

# args required to be able to run gdb/strace under the docker container
DOCKER_PRIVILEGED_ARGS=(
  # TODO(jtattermusch): document why exactly is this option needed.
  "--cap-add=SYS_PTRACE"
)

# propagate the well known variables to the docker container
DOCKER_PROPAGATE_ENV_ARGS=(
  "--env-file=tools/run_tests/dockerize/docker_propagate_env.list"
)

DOCKER_CLEANUP_ARGS=(
  # delete the container when the containers exits
  # (otherwise the container will not release the disk space it used)
  "--rm=true"
)

DOCKER_NETWORK_ARGS=(
  # enable IPv6
  "--sysctl=net.ipv6.conf.all.disable_ipv6=0"
)

DOCKER_IMAGE_IDENTITY_ARGS=(
  # make the info about docker image used available from inside the docker container
  "-e=GRPC_TEST_DOCKER_IMAGE_IDENTITY=${DOCKER_IMAGE_NAME}"
)

# TODO: silence complaint about lack of quotes in some other way
# shellcheck disable=SC2206
DOCKER_EXTRA_ARGS_FROM_ENV=(
  # Not quoting the variable is intentional, since the env variable may contain
  # multiple arguments and we want to interpret it as such.
  # TODO: get rid of EXTRA_DOCKER_ARGS occurrences and replace with DOCKER_EXTRA_ARGS
  ${EXTRA_DOCKER_ARGS}
  ${DOCKER_EXTRA_ARGS}
)

# Git root as seen by the docker instance
# TODO(jtattermusch): rename to a more descriptive directory name
# currently that's nontrivial because the name is hardcoded in many places.
EXTERNAL_GIT_ROOT=/var/local/jenkins/grpc

MOUNT_GIT_ROOT_ARGS=(
  "-v=${git_root}:${EXTERNAL_GIT_ROOT}"
  "-e=EXTERNAL_GIT_ROOT=${EXTERNAL_GIT_ROOT}"
)

# temporary directory that will be mounted to the docker container
# as a way to persist report files.
TEMP_REPORT_DIR="$(mktemp -d)"
# make sure the "reports" dir exists and is owned by current user.
mkdir -p "${TEMP_REPORT_DIR}/reports"
mkdir -p "${git_root}/reports"

MOUNT_REPORT_DIR_ARGS=(
  # mount the temporary directory as the "report dir".
  "-v=${TEMP_REPORT_DIR}:/var/local/report_dir"
  # make the reports/ directory show up under the mounted git root
  "-v=${TEMP_REPORT_DIR}/reports:${EXTERNAL_GIT_ROOT}/reports"
  # set GRPC_TEST_REPORT_BASE_DIR to make sure that when XML test reports
  # are generated, they will be stored in the report dir.
  "-e=GRPC_TEST_REPORT_BASE_DIR=/var/local/report_dir"
)

if [ "${OUTPUT_DIR}" != "" ]
then
  # temporary directory that will be mounted to the docker container
  # as a way to persist output files.
  # use unique name for the output directory to prevent clash between concurrent
  # runs of multiple docker containers
  TEMP_OUTPUT_DIR="$(mktemp -d)"

  # make sure the "${OUTPUT_DIR}" dir exists and is owned by current user.
  mkdir -p "${TEMP_OUTPUT_DIR}/${OUTPUT_DIR}"
  mkdir -p "${git_root}/${OUTPUT_DIR}"

  MOUNT_OUTPUT_DIR_ARGS=(
    # the OUTPUT_DIR refers to a subdirectory of the git root.
    "-v=${TEMP_OUTPUT_DIR}/${OUTPUT_DIR}:${EXTERNAL_GIT_ROOT}/${OUTPUT_DIR}"
    "-e=OUTPUT_DIR=${OUTPUT_DIR}"
  )
else
  MOUNT_OUTPUT_DIR_ARGS=()
fi

# Run tests inside docker
DOCKER_EXIT_CODE=0

docker run \
  "${DOCKER_TTY_ARGS[@]}" \
  "${DOCKER_RUN_SCRIPT_ARGS[@]}" \
  "${MOUNT_KEYSTORE_DIR_ARGS[@]}" \
  "${MOUNT_GFILE_DIR_ARGS[@]}" \
  "${MOUNT_ARTIFACTS_DIR_ARGS[@]}" \
  "${DOCKER_PRIVILEGED_ARGS[@]}" \
  "${DOCKER_PROPAGATE_ENV_ARGS[@]}" \
  "${DOCKER_CLEANUP_ARGS[@]}" \
  "${DOCKER_NETWORK_ARGS[@]}" \
  "${DOCKER_IMAGE_IDENTITY_ARGS[@]}" \
  "${MOUNT_GIT_ROOT_ARGS[@]}" \
  "${MOUNT_REPORT_DIR_ARGS[@]}" \
  "${MOUNT_OUTPUT_DIR_ARGS[@]}" \
  "${DOCKER_EXTRA_ARGS_FROM_ENV[@]}" \
  "${DOCKER_IMAGE_NAME}" \
  "${DOCKER_CMD_AND_ARGS[@]}" || DOCKER_EXIT_CODE=$?

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
  cp -r "${TEMP_OUTPUT_DIR}/${OUTPUT_DIR}" "${git_root}" || DOCKER_EXIT_CODE=$?
fi

exit $DOCKER_EXIT_CODE
