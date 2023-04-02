#!/bin/bash
# Copyright 2018 gRPC authors.
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

# Don't run this script standalone. Instead, run from the repository root:
# ./tools/run_tests/run_tests.py -l objc

set -ex
set -o pipefail  # preserve xcodebuild exit code when piping output

cd "$(dirname "$0")"

XCODEBUILD_FILTER_OUTPUT_SCRIPT="../../../../../src/objective-c/tests/xcodebuild_filter_output.sh"

XCODEBUILD_FLAGS="
  IPHONEOS_DEPLOYMENT_TARGET=10
"

XCODEBUILD_DESTINATION="platform=iOS Simulator,name=iPhone 11"

time ./build_tests.sh

time xcodebuild \
    -workspace CFStreamTests.xcworkspace \
    -scheme CFStreamTests \
    -destination "${XCODEBUILD_DESTINATION}" \
    test \
    "${XCODEBUILD_FLAGS}" \
    | "${XCODEBUILD_FILTER_OUTPUT_SCRIPT}"

time xcodebuild \
    -workspace CFStreamTests.xcworkspace \
    -scheme CFStreamTests_Asan \
    -destination "${XCODEBUILD_DESTINATION}" \
    test \
    "${XCODEBUILD_FLAGS}" \
    | "${XCODEBUILD_FILTER_OUTPUT_SCRIPT}"

time xcodebuild \
    -workspace CFStreamTests.xcworkspace \
    -scheme CFStreamTests_Tsan \
    -destination "${XCODEBUILD_DESTINATION}" \
    test \
    "${XCODEBUILD_FLAGS}" \
    | "${XCODEBUILD_FILTER_OUTPUT_SCRIPT}"

time xcodebuild \
    -workspace CFStreamTests.xcworkspace \
    -scheme CFStreamTests_Msan \
    -destination "${XCODEBUILD_DESTINATION}" \
    test \
    "${XCODEBUILD_FLAGS}" \
    | "${XCODEBUILD_FILTER_OUTPUT_SCRIPT}"
