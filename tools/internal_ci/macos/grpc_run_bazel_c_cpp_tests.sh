#!/usr/bin/env bash
# Copyright 2019 The gRPC Authors
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

set -ex

# change to grpc repo root
cd $(dirname $0)/../../..

# TODO(jtattermusch): most of the prepare steps should not be needed for bazel
source tools/internal_ci/helper_scripts/prepare_build_macos_rc

# make sure bazel is available
tools/bazel version

./tools/run_tests/start_port_server.py

# to get "bazel" link for kokoro build, we need to generate
# invocation UUID, set a flag for bazel to use it
# and upload "bazel_invocation_ids" file as artifact.
# NOTE: UUID needs to be in lowercase for the result link to work
# (on mac "uuidgen" outputs uppercase UUID)
BAZEL_INVOCATION_ID="$(uuidgen | tr '[:upper:]' '[:lower:]')"
echo "${BAZEL_INVOCATION_ID}" >"${KOKORO_ARTIFACTS_DIR}/bazel_invocation_ids"

# run all C/C++ tests
tools/bazel \
  --bazelrc=tools/remote_build/mac.bazelrc \
  test \
  --invocation_id="${BAZEL_INVOCATION_ID}" \
  --workspace_status_command=tools/remote_build/workspace_status_kokoro.sh \
  --google_credentials="${KOKORO_GFILE_DIR}/GrpcTesting-d0eeee2db331.json" \
  $BAZEL_FLAGS \
  -- //test/... || FAILED="true"

if [ "$UPLOAD_TEST_RESULTS" != "" ]
then
  # Sleep to let ResultStore finish writing results before querying
  sleep 60
  PYTHONHTTPSVERIFY=0 python ./tools/run_tests/python_utils/upload_rbe_results.py
fi

if [ "$FAILED" != "" ]
then
  exit 1
fi
