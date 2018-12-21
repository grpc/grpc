#!/usr/bin/env bash
# Copyright 2017 gRPC authors.
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

# A temporary solution to give Kokoro credentials.
# The file name 4321_grpc-testing-service needs to match auth_credential in
# the build config.
mkdir -p ${KOKORO_KEYSTORE_DIR}
cp ${KOKORO_GFILE_DIR}/GrpcTesting-d0eeee2db331.json ${KOKORO_KEYSTORE_DIR}/4321_grpc-testing-service

# Download bazel
temp_dir="$(mktemp -d)"
wget -q https://github.com/bazelbuild/bazel/releases/download/0.17.1/bazel-0.17.1-linux-x86_64 -O "${temp_dir}/bazel"
chmod 755 "${temp_dir}/bazel"
export PATH="${temp_dir}:${PATH}"
# This should show ${temp_dir}/bazel
which bazel

# change to grpc repo root
cd $(dirname $0)/../../..

source tools/internal_ci/helper_scripts/prepare_build_linux_rc

# to get "bazel" link for kokoro build, we need to generate
# invocation UUID, set a flag for bazel to use it
# and upload "bazel_invocation_ids" file as artifact.
BAZEL_INVOCATION_ID="$(uuidgen)"
echo "${BAZEL_INVOCATION_ID}" >"${KOKORO_ARTIFACTS_DIR}/bazel_invocation_ids"

bazel \
  --bazelrc=tools/remote_build/kokoro.bazelrc \
  test \
  --invocation_id="${BAZEL_INVOCATION_ID}" \
  --workspace_status_command=tools/remote_build/workspace_status_kokoro.sh \
  $@ \
  -- //test/... || FAILED="true"

if [ "$UPLOAD_TEST_RESULTS" != "" ]
then
  # Sleep to let ResultStore finish writing results before querying
  sleep 60
  python ./tools/run_tests/python_utils/upload_rbe_results.py
fi

if [ "$FAILED" != "" ]
then
  exit 1
fi
