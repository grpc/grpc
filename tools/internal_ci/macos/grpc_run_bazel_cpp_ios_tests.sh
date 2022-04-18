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

./tools/run_tests/start_port_server.py

# only select ObjC test from the following subdirs
# TODO(jtattermusch): start running selected tests from //test/core too.
test_pattern="//test/cpp/end2end/... + //test/cpp/server/... + //test/cpp/client/... + //test/cpp/common/... + //test/cpp/codegen/... + //test/cpp/util/... + //test/cpp/grpclb/... + //test/cpp/test/..."

# iOS tests are marked as "manual" to prevent them from running by default. To run them, we need to use "bazel query" to list them first.
ios_tests=$(tools/bazel query "kind(ios_unit_test, tests($test_pattern))" | grep '^//')

python3 tools/run_tests/python_utils/bazel_report_helper.py --report_path bazel_cpp_ios_tests

bazel_cpp_ios_tests/bazel_wrapper \
  --bazelrc=tools/remote_build/include/test_locally_with_resultstore_results.bazelrc \
  test \
  --google_credentials="${KOKORO_GFILE_DIR}/GrpcTesting-d0eeee2db331.json" \
  $BAZEL_FLAGS \
  -- ${ios_tests}
