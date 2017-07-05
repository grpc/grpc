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

./tools/run_tests/start_port_server.py || true

CPUS=`python -c 'import multiprocessing; print multiprocessing.cpu_count()'`

make CONFIG=opt memory_profile_test memory_profile_client memory_profile_server -j $CPUS
bins/opt/memory_profile_test
bq load microbenchmarks.memory memory_usage.csv

tools/run_tests/run_microbenchmark.py --collect summary --bigquery_upload
