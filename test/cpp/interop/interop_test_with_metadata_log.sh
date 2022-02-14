#!/bin/bash
# Copyright 2021 The gRPC Authors
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

LOGFILE="$(mktemp)"

# Location of the interop test binary is different depending
# on whether we are running externally/internally, so we try both.
INTEROP_TEST_BINARY="test/cpp/interop/interop_test"
test -x "${INTEROP_TEST_BINARY}" || INTEROP_TEST_BINARY="third_party/grpc/${INTEROP_TEST_BINARY}"

# Test that the --log_metadata_and_status flag generates the expected logs in the right format
# by running the "custom_metadata" interop test case and inspect the logs afterwards.
"${INTEROP_TEST_BINARY}" --extra_client_flags="--test_case=custom_metadata,--log_metadata_and_status" 2>&1 | tee "${LOGFILE}"

# grep will return an error if any of the patterns below are not found in the log
grep -q "GRPC_INITIAL_METADATA x-grpc-test-echo-initial: test_initial_metadata_value" "${LOGFILE}"
grep -q "GRPC_TRAILING_METADATA x-grpc-test-echo-trailing-bin: CgsKCwoL" "${LOGFILE}"
grep -q "GRPC_ERROR_MESSAGE " "${LOGFILE}"
grep -q "GRPC_STATUS 0" "${LOGFILE}"

echo "Passed."
