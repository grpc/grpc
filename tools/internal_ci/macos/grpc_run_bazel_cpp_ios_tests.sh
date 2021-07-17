#!/usr/bin/env bash
# Copyright 2019 The gRPC Authors
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

./tools/run_tests/start_port_server.py

dirs=(end2end server client common codegen util grpclb test)
for dir in ${dirs[*]}; do
  echo $dir
  out=`tools/bazel query "kind(ios_unit_test, tests(//test/cpp/$dir/...))" 2>/dev/null | grep '^//'`
  for test in $out; do
    echo "Running: $test"
    tools/bazel test --test_summary=detailed --test_output=all $test
  done
done
