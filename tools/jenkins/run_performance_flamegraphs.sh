#!/usr/bin/env bash
# Copyright 2015, Google Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
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

