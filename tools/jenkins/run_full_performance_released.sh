#!/usr/bin/env bash
# Copyright 2017, Google Inc.
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
