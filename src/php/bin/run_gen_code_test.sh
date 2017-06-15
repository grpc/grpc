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

set -e
cd $(dirname $0)
source ./determine_extension_dir.sh
export GRPC_TEST_HOST=localhost:50051
php $extension_dir -d max_execution_time=300 $(which phpunit) -v --debug \
  ../tests/generated_code/GeneratedCodeTest.php
php $extension_dir -d max_execution_time=300 $(which phpunit) -v --debug \
  ../tests/generated_code/GeneratedCodeWithCallbackTest.php
