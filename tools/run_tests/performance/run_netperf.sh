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

set -ex

cd $(dirname $0)/../../..

netperf >netperf_latency.txt -P 0 -t TCP_RR -H "$NETPERF_SERVER_HOST" -- -r 1,1 -o P50_LATENCY,P90_LATENCY,P99_LATENCY

cat netperf_latency.txt

if [ "$BQ_RESULT_TABLE" != "" ]
then
  tools/run_tests/performance/bq_upload_result.py \
      --file_to_upload=netperf_latency.txt \
      --file_format=netperf_latency_csv \
      --bq_result_table="$BQ_RESULT_TABLE"
fi
