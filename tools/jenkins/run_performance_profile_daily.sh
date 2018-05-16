#!/bin/bash
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

set -ex

cd $(dirname $0)/../..

# try to use pypy for generating reports
# each trace dumps 7-8gig of text to disk, and processing this into a report is
# heavyweight - so any speed boost is worthwhile
# TODO(ctiller): consider rewriting report generation in C++ for performance
if which pypy >/dev/null; then
  PYTHON=pypy
else
  PYTHON=python2.7
fi

BENCHMARKS_TO_RUN="bm_fullstack_unary_ping_pong bm_fullstack_streaming_ping_pong bm_fullstack_streaming_pump bm_closure bm_cq bm_call_create bm_error bm_chttp2_hpack bm_chttp2_transport bm_pollset bm_metadata"

./tools/run_tests/start_port_server.py || true

$PYTHON tools/run_tests/run_microbenchmark.py --collect summary perf latency -b $BENCHMARKS_TO_RUN
