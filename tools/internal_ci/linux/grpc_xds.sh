#!/usr/bin/env bash
# Copyright 2020 gRPC authors.
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

# change to grpc repo root
cd $(dirname $0)/../../..

tools/run_tests/helper_scripts/prep_xds.sh
python3 tools/run_tests/run_xds_tests.py \
    --test_case=all \
    --project_id=grpc-testing \
    --gcp_suffix=$(date '+%s') \
    --verbose \
    --client_cmd='bazel run test/cpp/interop:xds_interop_client -- --server=xds-experimental:///{service_host}:{service_port} --stats_port={stats_port} --qps={qps}'
