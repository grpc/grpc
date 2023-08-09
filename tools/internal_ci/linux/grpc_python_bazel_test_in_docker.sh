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

set -ex

RESULTSTORE_RESULTS_FLAG="--bazelrc=tools/remote_build/include/test_locally_with_resultstore_results.bazelrc"
TEST_TARGETS="//src/python/... //tools/distrib/python/grpcio_tools/... //examples/python/..."
BAZEL_FLAGS="--test_output=errors"

python3 tools/run_tests/python_utils/bazel_report_helper.py --report_path python_bazel_tests
python_bazel_tests/bazel_wrapper ${RESULTSTORE_RESULTS_FLAG} test ${BAZEL_FLAGS} ${TEST_TARGETS}

python3 tools/run_tests/python_utils/bazel_report_helper.py --report_path python_bazel_tests_single_threaded_unary_streams
python_bazel_tests_single_threaded_unary_streams/bazel_wrapper ${RESULTSTORE_RESULTS_FLAG} test --config=python_single_threaded_unary_stream ${BAZEL_FLAGS} ${TEST_TARGETS}

python3 tools/run_tests/python_utils/bazel_report_helper.py --report_path python_bazel_tests_poller_engine
python_bazel_tests_poller_engine/bazel_wrapper ${RESULTSTORE_RESULTS_FLAG} test --config=python_poller_engine ${BAZEL_FLAGS} ${TEST_TARGETS}

python3 tools/run_tests/python_utils/bazel_report_helper.py --report_path python_bazel_tests_fork_support

# TODO(https://github.com/grpc/grpc/issues/32207): Remove from this job once
# the fork job is in the master dashboard.
python_bazel_tests_fork_support/bazel_wrapper ${RESULTSTORE_RESULTS_FLAG} test --config=fork_support --runs_per_test=16 ${BAZEL_FLAGS} //src/python/grpcio_tests/tests/fork:fork_test
