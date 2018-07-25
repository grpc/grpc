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
set -ex

# Enter the gRPC repo root
cd $(dirname $0)/../../..

source tools/internal_ci/helper_scripts/prepare_build_linux_perf_multilang_rc

# run 8core client vs 8core server
tools/run_tests/run_performance_tests.py \
    -l c++ csharp ruby java python go php7 php7_protobuf_c \
    --netperf \
    --category scalable \
    --remote_worker_host grpc-kokoro-performance-server-8core grpc-kokoro-performance-client-8core grpc-kokoro-performance-client2-8core \
    -u kbuilder \
    --bq_result_table performance_test.performance_experiment \
    --xml_report reports/8core/sponge_log.xml \
    || EXIT_CODE=1

# prevent pushing leftover build files to remote hosts in the next step.
git clean -fdxq -e reports

# scalability with 32cores (and upload to a different BQ table)
tools/run_tests/run_performance_tests.py \
    -l c++ java csharp go \
    --netperf \
    --category scalable \
    --remote_worker_host grpc-kokoro-performance-server-32core grpc-kokoro-performance-client-32core grpc-kokoro-performance-client2-32core \
    -u kbuilder \
    --bq_result_table performance_test.performance_experiment_32core \
    --xml_report reports/32core/sponge_log.xml \
    || EXIT_CODE=1

# prevent pushing leftover build files to remote hosts in the next step.
git clean -fdxq -e reports

# selected scenarios on Windows
tools/run_tests/run_performance_tests.py \
    -l csharp \
    --category scalable \
    --remote_worker_host grpc-kokoro-performance-windows1 grpc-kokoro-performance-windows2 \
    --bq_result_table performance_test.performance_experiment_windows \
    --xml_report reports/windows/sponge_log.xml \
    || EXIT_CODE=1

exit $EXIT_CODE
