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
# Generates index.html that will contain links to various test results on kokoro.
set -e

# change to grpc repo root
cd $(dirname $0)/../../..

# Kororo URLs are in the form "grpc/job/macos/job/master/job/grpc_build_artifacts"
KOKORO_JOB_PATH=$(echo "${KOKORO_JOB_NAME}" | sed "s|/|/job/|g")

mkdir -p reports

echo '<html><head></head><body>' > reports/kokoro_index.html
echo '<h1>'${KOKORO_JOB_NAME}', build '#${KOKORO_BUILD_NUMBER}'</h1>' >> reports/kokoro_index.html
echo '<h2><a href="https://kokoro.corp.google.com/job/'${KOKORO_JOB_PATH}'/'${KOKORO_BUILD_NUMBER}'/">Kokoro build dashboard (internal only)</a></h2>' >> reports/kokoro_index.html
echo '<h2><a href="https://sponge.corp.google.com/invocation?id='${KOKORO_BUILD_ID}'&searchFor=">Test result dashboard (internal only)</a></h2>' >> reports/kokoro_index.html
echo '<h2><a href="test_report.html">HTML test report (Not available yet)</a></h2>' >> reports/kokoro_index.html
echo '<h2><a href="test_log.txt">Test log (Not available yet)</a></h2>' >> reports/kokoro_index.html
echo '</body></html>' >> reports/kokoro_index.html

echo 'Created reports/kokoro_index.html report index'
