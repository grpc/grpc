#!/bin/bash
Copyright 2015 gRPC authors.
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

EXIT_CODE=0

if [[ $LD_PRELOAD != '/usr/lib/gcc/x86_64-linux-gnu/4.9/libasan.so' ]];
then
  echo "Got $LD_PRELOAD for LD_PRELOAD. Sanity check failed."
  exit 101
fi

cd src/ruby/spec
test_cases=(`find -name '*_spec.rb'`)

# Run each test as a separate process to make heap checker more granular
for test in ${test_cases[@]}; do
  rspec "$test" || EXIT_CODE=1
done;

exit $EXIT_CODE
