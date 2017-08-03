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
# A frozen version of run_full_performance.sh that runs full performance test
# suite for the latest released stable version of gRPC.
set -ex

# Enter the gRPC repo root
cd $(dirname $0)/../..

# run 8core client vs 8core server
tools/run_tests/run_performance_tests.py \
    -l c++ csharp node ruby java python go node_express \
    --netperf \
    --category scalable \
    --bq_result_table performance_released.performance_experiment \
    --remote_worker_host grpc-performance-server-8core grpc-performance-client-8core grpc-performance-client2-8core \
    --xml_report report_8core.xml \
    || EXIT_CODE=1

# prevent pushing leftover build files to remote hosts in the next step.
git clean -fdxq --exclude='report*.xml'

# scalability with 32cores (and upload to a different BQ table)
tools/run_tests/run_performance_tests.py \
    -l c++ java csharp go \
    --netperf \
    --category scalable \
    --bq_result_table performance_released.performance_experiment_32core \
    --remote_worker_host grpc-performance-server-32core grpc-performance-client-32core grpc-performance-client2-32core \
    --xml_report report_32core.xml \
    || EXIT_CODE=1

# prevent pushing leftover build files to remote hosts in the next step.
git clean -fdxq --exclude='report*.xml'

# selected scenarios on Windows
tools/run_tests/run_performance_tests.py \
    -l csharp \
    --category scalable \
    --bq_result_table performance_released.performance_experiment_windows \
    --remote_worker_host grpc-performance-windows1 grpc-performance-windows2 \
    --xml_report report_windows.xml \
    || EXIT_CODE=1

exit $EXIT_CODE
