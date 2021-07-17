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

set -ev
set -o pipefail

cd "$(dirname "$0")"

echo "TIME:  $(date)"

./build_tests.sh

echo "TIME:  $(date)"

XCODEBUILD_FILTER='(^CompileC |^Ld |^ *[^ ]*clang |^ *cd |^ *export |^Libtool |^ *[^ ]*libtool |^CpHeader |^ *builtin-copy )'

xcodebuild \
    -workspace CFStreamTests.xcworkspace \
    -scheme CFStreamTests \
    -destination name="iPhone 8" \
    test \
    | grep -E -v "$XCODEBUILD_FILTER" \
    | grep -E -v '^$' \
    | grep -E -v "(GPBDictionary|GPBArray)" -

echo "TIME:  $(date)"

xcodebuild \
    -workspace CFStreamTests.xcworkspace \
    -scheme CFStreamTests_Asan \
    -destination name="iPhone 8" \
    test \
    | grep -E -v "$XCODEBUILD_FILTER" \
    | grep -E -v '^$' \
    | grep -E -v "(GPBDictionary|GPBArray)" -

echo "TIME:  $(date)"

xcodebuild \
    -workspace CFStreamTests.xcworkspace \
    -scheme CFStreamTests_Tsan \
    -destination name="iPhone 8" \
    test \
    | grep -E -v "$XCODEBUILD_FILTER" \
    | grep -E -v '^$' \
    | grep -E -v "(GPBDictionary|GPBArray)" -

echo "TIME:  $(date)"

xcodebuild \
    -workspace CFStreamTests.xcworkspace \
    -scheme CFStreamTests_Msan \
    -destination name="iPhone 8" \
    test \
    | grep -E -v "$XCODEBUILD_FILTER" \
    | grep -E -v '^$' \
    | grep -E -v "(GPBDictionary|GPBArray)" -
