#! /bin/bash
# Copyright 2019 The gRPC Authors
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

# This test benchmarks Python client/server.
set -ex

declare -a DRIVER_PORTS=("10086" "10087")
SCENARIOS_FILE=src/python/grpcio_tests/tests/qps/scenarios.json

function join { local IFS="$1"; shift; echo "$*"; }

if [[ -e "${SCENARIOS_FILE}" ]]; then
    echo "Running against scenarios.json:"
    cat "${SCENARIOS_FILE}"
else
    echo "Failed to find scenarios.json!"
    exit 1
fi

echo "Starting Python qps workers..."
qps_workers=()
for DRIVER_PORT in "${DRIVER_PORTS[@]}"
do
    echo -e "\tRunning Python qps worker listening at localhost:${DRIVER_PORT}..."
    src/python/grpcio_tests/tests/qps/qps_worker \
      --driver_port="${DRIVER_PORT}" &
    qps_workers+=("localhost:${DRIVER_PORT}")
done

echo "Running qps json driver..."
QPS_WORKERS=$(join , ${qps_workers[@]})
export QPS_WORKERS
test/cpp/qps/qps_json_driver --scenarios_file="${SCENARIOS_FILE}"
