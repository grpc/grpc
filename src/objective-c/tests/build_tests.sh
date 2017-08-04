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

# Don't run this script standalone. Instead, run from the repository root:
# ./tools/run_tests/run_tests.py -l objc

set -e

# CocoaPods requires the terminal to be using UTF-8 encoding.
export LANG=en_US.UTF-8

cd $(dirname $0)

hash pod 2>/dev/null || { echo >&2 "Cocoapods needs to be installed."; exit 1; }
hash xcodebuild 2>/dev/null || {
    echo >&2 "XCode command-line tools need to be installed."
    exit 1
}

# clean the directory
rm -rf Pods
rm -rf Tests.xcworkspace
rm -f Podfile.lock
rm -f RemoteTestClient/*.{h,m}

echo "TIME:  $(date)"
pod install

