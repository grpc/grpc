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
# This script is invoked by Jenkins and runs a diff on the microbenchmarks
set -ex

# List of benchmarks that provide good signal for analyzing performance changes in pull requests
BENCHMARKS_TO_RUN="bm_fullstack_unary_ping_pong bm_fullstack_streaming_ping_pong bm_fullstack_streaming_pump bm_closure bm_cq bm_call_create bm_error bm_chttp2_hpack bm_chttp2_transport bm_pollset bm_metadata"

# Enter the gRPC repo root
cd $(dirname $0)/../..

tools/run_tests/start_port_server.py
tools/profiling/microbenchmarks/bm_diff/bm_main.py -d origin/$ghprbTargetBranch -b $BENCHMARKS_TO_RUN
