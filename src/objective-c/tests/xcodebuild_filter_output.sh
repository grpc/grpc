#!/bin/bash
# Copyright 2021 The gRPC Authors
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

# Be default xcodebuild generates gigabytes of useless output. We
# use this script to make the logs smaller, while still keeping them
# useful (e.g. we need to be able to see the logs printed by a test
# when it fails on the CI).
# Alternatives considered:
# * "xcodebuild -quiet" prints much less output, but doesn't display the test outputs.
# * "xcpretty" prints a nice and readable log, but doesn't display logs printed
#   by the tests when they fail (and we need those)
#
# Usage:
# set -o pipefail  # preserve xcodebuild's exit code when piping the output
# xcodebuild ... | xcodebuild_filter_output.sh

# need to be careful not to exclude important logs, so the patterns here need to be very specific.
XCODEBUILD_FILTER='(^CompileC |^Ld |^.*clang |^ *cd |^ *export |^Libtool |^.*libtool |^CpHeader |^ *builtin-copy )'

# we expect xcodebuild output piped to the stdin
# - also skip empty lines
grep -E -v "$XCODEBUILD_FILTER" - | grep -E -v "^$" -

# TODO: What is this for?
#| grep -E -v "(GPBDictionary|GPBArray)" -
