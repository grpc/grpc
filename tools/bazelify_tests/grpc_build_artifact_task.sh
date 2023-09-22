#!/bin/bash
# Copyright 2023 The gRPC Authors
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

set -e

ARCHIVE_WITH_SUBMODULES="$1"
BUILD_SCRIPT="$2"
EXIT_CODE_FILE="$3"
SCRIPT_LOG_FILE="$4"
ARTIFACTS_OUT_FILE="$5"
shift 5

# Extract grpc repo archive
tar -xopf ${ARCHIVE_WITH_SUBMODULES}
cd grpc

mkdir -p artifacts

# Run the build script with args, storing its stdout and stderr
# in a log file.
SCRIPT_EXIT_CODE=0
../"${BUILD_SCRIPT}" "$@" >"../${SCRIPT_LOG_FILE}" 2>&1  || SCRIPT_EXIT_CODE="$?"

# Store build script's exitcode in a file.
# Note that the build atifacts task will terminate with success even when
# there was an error building the artifacts.
# The error status (an associated log) will be reported by an associated
# bazel test.
echo "${SCRIPT_EXIT_CODE}" >"../${EXIT_CODE_FILE}"

# collect the artifacts
# TODO(jtattermusch): add tar flags to create deterministic tar archive
tar -czvf ../"${ARTIFACTS_OUT_FILE}" artifacts
