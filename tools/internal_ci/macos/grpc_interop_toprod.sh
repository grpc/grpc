#!/bin/bash
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

# change to grpc repo root
cd $(dirname $0)/../../..

source tools/internal_ci/helper_scripts/prepare_build_macos_interop_rc
source tools/internal_ci/helper_scripts/prepare_build_macos_rc

# using run_interop_tests.py without --use_docker, so we need to build first
tools/run_tests/run_tests.py -l c++ -c opt --build_only

export GRPC_DEFAULT_SSL_ROOTS_FILE_PATH="$(pwd)/etc/roots.pem"

# NOTE: only tests a subset of languages for time & dependency constraints
# building all languages in the same working copy can also lead to conflicts
# due to different compilation flags
tools/run_tests/run_interop_tests.py -l c++ \
    --cloud_to_prod --cloud_to_prod_auth --prod_servers default gateway_v4 \
    --service_account_key_file="${KOKORO_GFILE_DIR}/GrpcTesting-726eb1347f15.json" \
    --skip_compute_engine_creds --internal_ci -t -j 4
