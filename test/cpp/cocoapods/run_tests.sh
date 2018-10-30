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

export LANG=en_US.UTF-8

cd "$(dirname "$0")"

# Test if cocoapods are installed
hash pod 2>/dev/null || { echo >&2 "Cocoapods needs to be installed."; exit 1; }
hash xcodebuild 2>/dev/null || {
    echo >&2 "XCode command-line tools need to be installed."
    exit 1
}

# clean the directory
rm -rf Pods
rm -rf GRPCCppTests.xcworkspace
rm -f Podfile.lock

# install pods
echo "TIME:  $(date)"
pod install

set -o pipefail

# xcodebuild is very verbose. We filter its output and tell Bash to fail if any
# element of the pipe fails.
XCODEBUILD_FILTER='(^CompileC |^Ld |^ *[^ ]*clang |^ *cd |^ *export |^Libtool |^ *[^ ]*libtool |^CpHeader |^ *builtin-copy )'

echo "TIME:  $(date)"
xcodebuild \
    -workspace GRPCCppTests.xcworkspace \
    -scheme test \
    -destination name="iPhone 8" \
    test \
    | egrep -v "$XCODEBUILD_FILTER" \
    | egrep -v '^$' \
    | egrep -v "(GPBDictionary|GPBArray)" -

echo "TIME:  $(date)"
xcodebuild \
    -workspace GRPCCppTests.xcworkspace \
    -scheme generic \
    -destination name="iPhone 8" \
    test \
    | egrep -v "$XCODEBUILD_FILTER" \
    | egrep -v '^$' \
    | egrep -v "(GPBDictionary|GPBArray)" -

exit 0
