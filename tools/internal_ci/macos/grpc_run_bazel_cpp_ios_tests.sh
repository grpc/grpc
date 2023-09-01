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

# avoid slow finalization after the script has exited.
source $(dirname $0)/../../../tools/internal_ci/helper_scripts/move_src_tree_and_respawn_itself_rc

# change to grpc repo root
cd $(dirname $0)/../../..

source tools/internal_ci/helper_scripts/prepare_build_macos_rc

# make sure bazel is available
tools/bazel version

# for kokoro mac workers, exact image version is store in a well-known location on disk
KOKORO_IMAGE_VERSION="$(cat /VERSION)"

BAZEL_REMOTE_CACHE_ARGS=(
  # Enable uploading to remote cache. Requires the "roles/remotebuildexecution.actionCacheWriter" permission.
  --remote_upload_local_results=true
  # allow invalidating the old cache by setting to a new random key
  --remote_default_exec_properties="grpc_cache_silo_key1=83d8e488-1ca9-40fd-929e-d37d13529c99"
  # make sure we only get cache hits from binaries built on exact same macos image
  --remote_default_exec_properties="grpc_cache_silo_key2=${KOKORO_IMAGE_VERSION}"
)

./tools/run_tests/start_port_server.py

# only select ObjC test from the following subdirs
# TODO(jtattermusch): start running selected tests from //test/core too.
test_pattern="//test/cpp/end2end/... + //test/cpp/server/... + //test/cpp/client/... + //test/cpp/common/... + //test/cpp/codegen/... + //test/cpp/util/... + //test/cpp/grpclb/... + //test/cpp/test/... + //test/core/event_engine/..."

# iOS tests are marked as "manual" to prevent them from running by default. To run them, we need to use "bazel query" to list them first.
ios_tests=$(tools/bazel query "kind(ios_unit_test, tests($test_pattern))" | grep '^//')

python3 tools/run_tests/python_utils/bazel_report_helper.py --report_path bazel_cpp_ios_tests

bazel_cpp_ios_tests/bazel_wrapper \
  --bazelrc=tools/remote_build/include/test_locally_with_resultstore_results.bazelrc \
  test \
  --google_credentials="${KOKORO_GFILE_DIR}/GrpcTesting-d0eeee2db331.json" \
  "${BAZEL_REMOTE_CACHE_ARGS[@]}" \
  $BAZEL_FLAGS \
  -- ${ios_tests}
