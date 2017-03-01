#!/bin/bash
# Copyright 2016, gRPC authors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Don't run this script standalone. Instead, run from the repository root:
# ./tools/run_tests/run_tests.py -l objc

set -evo pipefail

cd `dirname $0`

SCHEME=HelloWorld                              \
  EXAMPLE_PATH=examples/objective-c/helloworld \
  ./build_one_example.sh

SCHEME=RouteGuideClient                         \
  EXAMPLE_PATH=examples/objective-c/route_guide \
  ./build_one_example.sh

SCHEME=AuthSample                               \
  EXAMPLE_PATH=examples/objective-c/auth_sample \
  ./build_one_example.sh

rm -f ../examples/RemoteTestClient/*.{h,m}

SCHEME=Sample                                  \
  EXAMPLE_PATH=src/objective-c/examples/Sample \
  ./build_one_example.sh

SCHEME=Sample                                  \
  EXAMPLE_PATH=src/objective-c/examples/Sample \
  FRAMEWORKS=YES                               \
  ./build_one_example.sh

SCHEME=SwiftSample                                  \
  EXAMPLE_PATH=src/objective-c/examples/SwiftSample \
  ./build_one_example.sh
