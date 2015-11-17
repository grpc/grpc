#!/bin/bash
# Copyright 2015, Google Inc.
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

set -ex

CONFIG=${CONFIG:-opt}

# change to grpc repo root
cd $(dirname $0)/../..

root=`pwd`

if [ "$CONFIG" = "gcov" ]
then
  ./node_modules/.bin/istanbul cover --dir reports/node_coverage \
    -x **/interop/* ./node_modules/.bin/_mocha -- --timeout 8000 src/node/test
  cd build
  gcov Release/obj.target/grpc/ext/*.o
  lcov --base-directory . --directory . -c -o coverage.info
  lcov -e coverage.info '**/src/node/ext/*' -o coverage.info
  genhtml -o ../reports/node_ext_coverage --num-spaces 2 \
    -t 'Node gRPC test coverage' coverage.info --rc genhtml_hi_limit=95 \
    --rc genhtml_med_limit=80 --no-prefix
  echo '<html><head><meta http-equiv="refresh" content="0;URL=lcov-report/index.html"></head></html>' > \
    ../reports/node_coverage/index.html
else
  JUNIT_REPORT_PATH=src/node/reports.xml JUNIT_REPORT_STACK=1 ./node_modules/.bin/mocha --reporter mocha-jenkins-reporter src/node/test || true
fi
