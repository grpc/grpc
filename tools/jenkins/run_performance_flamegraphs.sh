#!/usr/bin/env bash
# Copyright 2015 gRPC authors.
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

# Enter the gRPC repo root
cd $(dirname $0)/../..

# scalability with 32cores c++ benchmarks
tools/run_tests/run_performance_tests.py \
    -l c++ \
    --category scalable \
    --remote_worker_host grpc-performance-server-32core grpc-performance-client-32core grpc-performance-client2-32core \
    --perf_args "record -F 97 --call-graph dwarf" \
    --flame_graph_reports cpp_flamegraphs \
    || EXIT_CODE=1

# scalability with 32cores go benchmarks
tools/run_tests/run_performance_tests.py \
    -l go \
    --category scalable \
    --remote_worker_host grpc-performance-server-32core grpc-performance-client-32core grpc-performance-client2-32core \
    --perf_args "record -F 97 -g" \
    --flame_graph_reports go_flamegraphs \
    || EXIT_CODE=1

exit $EXIT_CODE

