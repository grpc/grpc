#!/usr/bin/env bash
# Copyright 2018 gRPC authors.
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

# TODO(sergiitk): why are we not executing it on RBE?

PS4='+ [$(date "+%H:%M:%S %Z") ${BASH_SOURCE[0]}]\011 '
set -ex

# Needed for upload_rbe_results.py big_query_utils called by bazel_report_helper.py
# TODO(sergiitk): move to the main bazel image, similar to bazel_arm64
python3 -m pip install --user google-api-python-client oauth2client

RESULTSTORE_RESULTS_FLAG="--bazelrc=tools/remote_build/include/test_locally_with_resultstore_results.bazelrc"
TEST_TARGETS="//src/python/... //tools/distrib/python/grpcio_tools/... //examples/python/..."
BAZEL_FLAGS="${BAZEL_FLAGS:-} --test_output=errors --config=python"

# All tests
python3 tools/run_tests/python_utils/bazel_report_helper.py --report_path python_bazel_tests
python_bazel_tests/bazel_wrapper \
    ${RESULTSTORE_RESULTS_FLAG} \
    test \
    ${BAZEL_FLAGS} \
    -- \
    ${TEST_TARGETS}

# All tests with python_single_threaded_unary_stream
python3 tools/run_tests/python_utils/bazel_report_helper.py --report_path python_bazel_tests_single_threaded_unary_streams
python_bazel_tests_single_threaded_unary_streams/bazel_wrapper \
    ${RESULTSTORE_RESULTS_FLAG} \
    test \
    ${BAZEL_FLAGS} \
    --config=python_single_threaded_unary_stream \
    -- \
    ${TEST_TARGETS}

# Fork tests
python3 tools/run_tests/python_utils/bazel_report_helper.py --report_path python_bazel_tests_fork_support
python_bazel_tests_fork_support/bazel_wrapper \
    ${RESULTSTORE_RESULTS_FLAG} \
    test \
    ${BAZEL_FLAGS} \
    --config=fork_support \
    --runs_per_test=16 \
    -- \
    //src/python/grpcio_tests/tests/fork:fork_test
