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

# CocoaPods requires the terminal to be using UTF-8 encoding.
export LANG=en_US.UTF-8

cd "$(dirname "$0")"

# Check that required tools are available.
hash pod 2>/dev/null || { echo >&2 "Cocoapods needs to be installed."; exit 1; }
hash xcodebuild 2>/dev/null || {
    echo >&2 "XCode command-line tools need to be installed."
    exit 1
}

# clean up pod cache and existing pods
rm -Rf Pods Podfile.lock CFStreamTests.xcworkspace
time pod cache clean --all

time pod install --verbose

# ios-test-cfstream-tests flakes sometimes because of missing files in gRPC-Core,
# add some log to help find out the root cause.
# TODO(yulinliang): Delete it after solving the issue.
if [ -d "./Pods/Headers/Public/gRPC-Core/grpc/impl/codegen" ]
then
    echo "grpc/impl/codegen/ has been imported."
    number_of_files=$(find Pods/Headers/Public/gRPC-Core/grpc/impl/codegen -name "*.h" | wc -l)
    echo "The number of files in Pods/Headers/Public/gRPC-Core/grpc/impl/codegen/ is $number_of_files"
else
    echo "Error: grpc/impl/codegen/ hasn't been imported."
fi
