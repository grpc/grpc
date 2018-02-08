#!/bin/bash
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

mkdir -p "$OUTPUT_DIR"

PERF_DATA_FILE="${PERF_BASE_NAME}-perf.data"
PERF_SCRIPT_OUTPUT="${PERF_BASE_NAME}-out.perf"

# Generate Flame graphs
echo "running perf script on $PERF_DATA_FILE"
perf script -i "$PERF_DATA_FILE" > "$PERF_SCRIPT_OUTPUT"

~/FlameGraph/stackcollapse-perf.pl "$PERF_SCRIPT_OUTPUT" | ~/FlameGraph/flamegraph.pl > "${OUTPUT_DIR}/${OUTPUT_FILENAME}.svg"
