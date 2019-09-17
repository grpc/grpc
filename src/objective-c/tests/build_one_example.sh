#!/bin/bash
# Copyright 2016 gRPC authors.
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

# Params:
# EXAMPLE_PATH - directory of the example
# SCHEME - scheme of the example, used by xcodebuild

# CocoaPods requires the terminal to be using UTF-8 encoding.
export LANG=en_US.UTF-8

cd `dirname $0`/../../..

cd $EXAMPLE_PATH

# clean the directory
rm -rf Pods
rm -rf *.xcworkspace
rm -f Podfile.lock

pod install

set -o pipefail
XCODEBUILD_FILTER='(^CompileC |^Ld |^.*clang |^ *cd |^ *export |^Libtool |^.*libtool |^CpHeader |^ *builtin-copy )'
if [ "$SCHEME" == "tvOS-sample" ]; then
  xcodebuild \
    build \
    -workspace *.xcworkspace \
    -scheme $SCHEME \
    -destination generic/platform=tvOS \
    -derivedDataPath Build/Build \
    CODE_SIGN_IDENTITY="" \
    CODE_SIGNING_REQUIRED=NO \
    | egrep -v "$XCODEBUILD_FILTER" \
    | egrep -v "^$" -
else
  xcodebuild \
    build \
    -workspace *.xcworkspace \
    -scheme $SCHEME \
    -destination generic/platform=iOS \
    -derivedDataPath Build/Build \
    CODE_SIGN_IDENTITY="" \
    CODE_SIGNING_REQUIRED=NO \
    | egrep -v "$XCODEBUILD_FILTER" \
    | egrep -v "^$" -
fi

