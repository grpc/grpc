#!/bin/bash
# Copyright 2016, Google Inc.
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

# format argument via
# $ echo '{...}' | python -mjson.tool
read -r -d '' SCENARIOS_JSON_ARG <<'EOF'
{
    "scenarios": [
        {
            "benchmark_seconds": 5,
            "client_config": {
                "client_channels": 1,
                "client_type": "SYNC_CLIENT",
                "histogram_params": {
                    "max_possible": 60000000000.0,
                    "resolution": 0.01
                },
                "load_params": {
                    "closed_loop": {}
                },
                "outstanding_rpcs_per_channel": 1,
                "payload_config": {
                    "simple_params": {
                        "req_size": 0,
                        "resp_size": 0
                    }
                },
                "rpc_type": "UNARY",
                "security_params": {
                    "server_host_override": "foo.test.google.fr",
                    "use_test_ca": true
                }
            },
            "name": "cpp_protobuf_sync_unary_ping_pong_secure",
            "num_clients": 1,
            "num_servers": 1,
            "server_config": {
                "core_limit": 1,
                "security_params": {
                    "server_host_override": "foo.test.google.fr",
                    "use_test_ca": true
                },
                "server_type": "SYNC_SERVER"
            },
            "spawn_local_worker_count": 2,
            "warmup_seconds": 5
        }
    ]
}

EOF

set -ex

cd $(dirname $0)/../../..

CPUS=`python -c 'import multiprocessing; print multiprocessing.cpu_count()'`

# try to use pypy for generating reports
# each trace dumps 7-8gig of text to disk, and processing this into a report is
# heavyweight - so any speed boost is worthwhile
# TODO(ctiller): consider rewriting report generation in C++ for performance
if which pypy >/dev/null; then
  PYTHON=pypy
else
  PYTHON=python2.7
fi

make CONFIG=basicprof -j$CPUS qps_json_driver

mkdir -p reports
echo '<html><head></head><body>' > reports/index.html
bins/basicprof/qps_json_driver --scenarios_json="$SCENARIOS_JSON_ARG"
echo '<pre>' >> reports/index.html
$PYTHON tools/profiling/latency_profile/profile_analyzer.py \
    --source=latency_trace.txt --fmt=simple >> reports/index.html
echo '</pre>' >> reports/index.html
echo '</body></html>' >> reports/index.html
