#!/usr/bin/env bash
# Copyright 2025 gRPC authors.
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

echo "Will be using this file for testing grpc macos sonoma migration"

# Google VRP bounded ResultStore parser control. This intentionally contains
# no URL, external entity, file path, credential, or canary.
mkdir -p vrp_resultstore_control
cat > vrp_resultstore_control/sponge_log.xml <<'EOF'
<testsuites><testsuite id="1" package="grpc" name="vrp_resultstore_control" failures="0" errors="0"><testcase name="VRP_RESULTSTORE_CONTROL_20261119_A7E4" time="0.001" /></testsuite></testsuites>
EOF
