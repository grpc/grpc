#!/bin/bash
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
