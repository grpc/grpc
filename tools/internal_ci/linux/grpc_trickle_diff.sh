#!/usr/bin/env bash
# Copyright 2017 gRPC authors.
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
BENCHMARKS_TO_RUN="cli_transport_stalls_per_iteration cli_stream_stalls_per_iteration svr_transport_stalls_per_iteration svr_stream_stalls_per_iteration"

# Enter the gRPC repo root
cd $(dirname $0)/../../..

source tools/internal_ci/helper_scripts/prepare_build_linux_perf_rc

tools/run_tests/start_port_server.py
tools/jenkins/run_c_cpp_test.sh tools/profiling/microbenchmarks/bm_diff/bm_main.py \
  -d origin/$ghprbTargetBranch \
  -b bm_fullstack_trickle \
  -l 4 \
  -t $BENCHMARKS_TO_RUN \
  --no-counters \
  --pr_comment_name trickle || FAILED="true"

# kill port_server.py to prevent the build from hanging
ps aux | grep port_server\\.py | awk '{print $2}' | xargs kill -9

if [ "$FAILED" != "" ]
then
  exit 1
fi
