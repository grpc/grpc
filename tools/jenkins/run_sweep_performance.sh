#!/usr/bin/env bash
# Copyright 2016 gRPC authors.
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
#
# This script is invoked by Jenkins and runs full performance test suite.
set -ex

SERVER_HOST=${1:-grpc-performance-server-32core}
CLIENT_HOST1=${2:-grpc-performance-client-32core}
CLIENT_HOST2=${3:-grpc-performance-client2-32core}
# Enter the gRPC repo root
cd $(dirname $0)/../..

# scalability with 32cores (and upload to a different BQ table)
tools/run_tests/run_performance_tests.py \
    -l c++ \
    --category sweep \
    --bq_result_table performance_test.performance_experiment_32core \
    --remote_worker_host ${SERVER_HOST} ${CLIENT_HOST1} ${CLIENT_HOST2} \
    --perf_args "record -F 97 --call-graph dwarf" \
    || EXIT_CODE=1

exit $EXIT_CODE
