#!/usr/bin/env bash
# Copyright 2022 The gRPC Authors
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

# Make sure actions run by bazel can find python3.
# Without this the build will fail with "env: python3: No such file or directory".
# When on kokoro MacOS Mojave image.
sudo ln -s $(which python3) /usr/bin/python3 || true

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

EXAMPLE_TARGETS=(
  # TODO(jtattermusch): ideally we'd say "//src/objective-c/examples/..." but not all the targets currently build
  //src/objective-c/examples:Sample
  //src/objective-c/examples:tvOS-sample
)

TEST_TARGETS=(
  # TODO(jtattermusch): ideally we'd say "//src/objective-c/tests/..." but not all the targets currently build
  //src/objective-c/tests:InteropTestsLocalCleartext
  //src/objective-c/tests:InteropTestsLocalSSL
  //src/objective-c/tests:InteropTestsRemote
  //src/objective-c/tests:MacTests
  //src/objective-c/tests:UnitTests
  # TODO: Enable this again once @CronetFramework is working
  #//src/objective-c/tests:CppCronetTests
  #//src/objective-c/tests:CronetTests
  #//src/objective-c/tests:PerfTests
  //src/objective-c/tests:CFStreamTests
  //src/objective-c/tests:EventEngineTests
  //src/objective-c/tests:tvtests_build_test
  # codegen plugin tests
  //src/objective-c/tests:objc_codegen_plugin_test
  //src/objective-c/tests:objc_codegen_plugin_option_test
)

# === BEGIN SECTION: run interop_server on the background ====
# Before testing objC at all, build the interop server since many of the ObjC test rely on it.
# Use remote cache to build the interop_server binary as quickly as possible (interop_server
# is not what we want to test actually, we just use it as a backend for ObjC test).
# TODO(jtattermusch): can we make ObjC test not depend on running a local interop_server?
python3 tools/run_tests/python_utils/bazel_report_helper.py --report_path build_interop_server
build_interop_server/bazel_wrapper \
  --bazelrc=tools/remote_build/mac.bazelrc \
  build \
  --google_credentials="${KOKORO_GFILE_DIR}/GrpcTesting-d0eeee2db331.json" \
  "${BAZEL_REMOTE_CACHE_ARGS[@]}" \
  -- \
  //test/cpp/interop:interop_server

# Start port server and allocate ports to run interop_server
python3 tools/run_tests/start_port_server.py

PLAIN_PORT=$(curl localhost:32766/get)
TLS_PORT=$(curl localhost:32766/get)

INTEROP_SERVER_BINARY=bazel-bin/test/cpp/interop/interop_server
# run the interop server on the background. The port numbers must match TestConfigs in BUILD.
# TODO(jtattermusch): can we make the ports configurable (but avoid breaking bazel build cache at the same time?)
"${INTEROP_SERVER_BINARY}" --port=$PLAIN_PORT --max_send_message_size=8388608 &
"${INTEROP_SERVER_BINARY}" --port=$TLS_PORT --max_send_message_size=8388608 --use_tls &
# make sure the interop_server processes we started on the background are killed upon exit.
trap 'echo "KILLING interop_server binaries running on the background"; kill -9 $(jobs -p)' EXIT
# === END SECTION: run interop_server on the background ====

# Environment variables that will be visible to objc tests.
OBJC_TEST_ENV_ARGS=(
  --test_env=HOST_PORT_LOCAL=localhost:$PLAIN_PORT
  --test_env=HOST_PORT_LOCALSSL=localhost:$TLS_PORT
)

python3 tools/run_tests/python_utils/bazel_report_helper.py --report_path objc_bazel_tests

# NOTE: When using bazel to run the tests, test env variables like GRPC_VERBOSITY or GRPC_TRACE
# seem to be correctly applied to the test environment even when running tests on a simulator.
# The below configuration runs all the tests with --test_env=GRPC_VERBOSITY=debug, which makes
# the test logs much more useful.
objc_bazel_tests/bazel_wrapper \
  --bazelrc=tools/remote_build/include/test_locally_with_resultstore_results.bazelrc \
  test \
  --google_credentials="${KOKORO_GFILE_DIR}/GrpcTesting-d0eeee2db331.json" \
  "${BAZEL_REMOTE_CACHE_ARGS[@]}" \
  $BAZEL_FLAGS \
  "${OBJC_TEST_ENV_ARGS[@]}" \
  -- \
  "${EXAMPLE_TARGETS[@]}" \
  "${TEST_TARGETS[@]}"
