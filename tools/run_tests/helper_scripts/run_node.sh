#!/bin/bash
# Copyright 2015 gRPC authors.
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

NODE_VERSION=$1
source ~/.nvm/nvm.sh

nvm use $NODE_VERSION
set -ex

CONFIG=${CONFIG:-opt}

# change to grpc repo root
cd $(dirname $0)/../../..

root=`pwd`

test_directory='src/node/test'
timeout=8000

if [ "$CONFIG" = "gcov" ]
then
  ./node_modules/.bin/istanbul cover --dir reports/node_coverage \
    -x **/interop/* ./node_modules/.bin/_mocha -- --timeout $timeout $test_directory
  cp -r reports/node_coverage/lcov-report/* reports/node_coverage/
  cd build
  gcov Release/obj.target/grpc/ext/*.o
  lcov --base-directory . --directory . -c -o coverage.info
  lcov -e coverage.info '**/src/node/ext/*' -o coverage.info
  genhtml -o ../reports/node_ext_coverage --num-spaces 2 \
    -t 'Node gRPC test coverage' coverage.info --rc genhtml_hi_limit=95 \
    --rc genhtml_med_limit=80 --no-prefix
else
  JUNIT_REPORT_PATH=src/node/report.xml JUNIT_REPORT_STACK=1 \
    ./node_modules/.bin/mocha --timeout $timeout \
    --reporter mocha-jenkins-reporter $test_directory
fi
