#!/usr/bin/env bash
# Copyright 2026 gRPC authors.
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

# make sure bazel is available
tools/bazel version

./tools/run_tests/start_port_server.py

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

# This is added to resolve imports not found errors like
# ImportError: cannot import name 'auth' from 'google'
# Tests which fails when workaround is not executed are listed below -
# //src/python/grpcio_tests/tests/admin:admin_test
# //src/python/grpcio_tests/tests/csds:csds_test
# //src/python/grpcio_tests/tests/interop:_insecure_intraop_test
# //src/python/grpcio_tests/tests/interop:_secure_intraop_test
# //src/python/grpcio_tests/tests_aio/interop:local_interop_test
# //src/python/grpcio_tests/tests_py3_only/interop:xds_interop_client_test"
# TODO(asheshvidyut): figure out proper fix instead of workaround below
python3 -m pip install -r requirements.bazel.lock

# Test targets mirrored from tools/internal_ci/linux/grpc_python_bazel_test_in_docker.sh
TEST_TARGETS="//src/python/..."
BAZEL_FLAGS="--test_output=errors --config=python"

python3 tools/run_tests/python_utils/bazel_report_helper.py --report_path python_bazel_tests
# Run standard Python Bazel tests
python_bazel_tests/bazel_wrapper \
  --output_base=.bazel_rbe \
  --bazelrc=tools/remote_build/mac.bazelrc \
  test \
  --google_credentials="${KOKORO_GFILE_DIR}/GrpcTesting-d0eeee2db331.json" \
  "${BAZEL_REMOTE_CACHE_ARGS[@]}" \
  ${BAZEL_FLAGS} \
  -- \
  ${TEST_TARGETS}

python3 tools/run_tests/python_utils/bazel_report_helper.py --report_path python_bazel_tests_single_threaded_unary_streams
# Run single-threaded unary stream tests
# Note: MacOS might differ in threading behavior, but we keep parity with Linux config
python_bazel_tests_single_threaded_unary_streams/bazel_wrapper \
  --output_base=.bazel_rbe \
  --bazelrc=tools/remote_build/mac.bazelrc \
  test \
  --google_credentials="${KOKORO_GFILE_DIR}/GrpcTesting-d0eeee2db331.json" \
  "${BAZEL_REMOTE_CACHE_ARGS[@]}" \
  --config=python_single_threaded_unary_stream \
  ${BAZEL_FLAGS} \
  -- \
  ${TEST_TARGETS}

python3 tools/run_tests/python_utils/bazel_report_helper.py --report_path python_bazel_tests_fork_support
# Run fork support tests
# Note: Logic mirrored from tools/internal_ci/linux/grpc_python_bazel_test_fork_in_docker.sh
python_bazel_tests_fork_support/bazel_wrapper \
  --output_base=.bazel_rbe \
  --bazelrc=tools/remote_build/mac.bazelrc \
  test \
  --google_credentials="${KOKORO_GFILE_DIR}/GrpcTesting-d0eeee2db331.json" \
  "${BAZEL_REMOTE_CACHE_ARGS[@]}" \
  --config=fork_support \
  --runs_per_test=16 \
  ${BAZEL_FLAGS} \
  //src/python/grpcio_tests/tests/fork:fork_test
