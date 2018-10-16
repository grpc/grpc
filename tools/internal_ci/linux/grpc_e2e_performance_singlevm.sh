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

# "smoketest" scenarios on a single VM (=no remote VM for running qps_workers)
tools/run_tests/run_performance_tests.py \
    -l c++ csharp ruby java python go php7 php7_protobuf_c node node_purejs \
    --netperf \
    --category smoketest \
    -u kbuilder \
    --xml_report reports/singlemachine/sponge_log.xml
