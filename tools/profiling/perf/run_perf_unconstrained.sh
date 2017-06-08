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

# format argument via
# $ echo '{...}' | python -mjson.tool
read -r -d '' SCENARIOS_JSON_ARG <<'EOF'
{
    "scenarios": [
        {
            "benchmark_seconds": 60,
            "warmup_seconds": 5,
            "client_config": {
                "client_channels": 100,
                "client_type": "ASYNC_CLIENT",
                "histogram_params": {
                    "max_possible": 60000000000.0,
                    "resolution": 0.01
                },
                "load_params": {
                    "closed_loop": {}
                },
                "outstanding_rpcs_per_channel": 100,
                "payload_config": {
                    "simple_params": {
                        "req_size": 0,
                        "resp_size": 0
                    }
                },
                "rpc_type": "UNARY",
                "security_params": null
            },
            "name": "name_goes_here",
            "num_clients": 1,
            "num_servers": 1,
            "server_config": {
                "security_params": null,
                "server_type": "ASYNC_SERVER"
            },
            "spawn_local_worker_count": -2
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

export config=mutrace

make CONFIG=$config -j$CPUS qps_json_driver

sudo perf record -F 997 -g bins/$config/qps_json_driver --scenarios_json="$SCENARIOS_JSON_ARG"
sudo perf report

