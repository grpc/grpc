#!/usr/bin/env bash
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
#
# This script is invoked by Jenkins and triggers run of distribution tests.
#
# To prevent cygwin bash complaining about empty lines ending with \r
# we set the igncr option. The option doesn't exist on Linux, so we fallback
# to just 'set -ex' there.
# NOTE: No empty lines should appear in this file before igncr is set!
set -ex -o igncr || set -ex

curr_platform="$platform"
unset platform  # variable named 'platform' breaks the windows build

# Try collecting the artifacts to test from previous Jenkins build step
mkdir -p input_artifacts
cp -r platform=windows/artifacts/* input_artifacts || true
cp -r platform=linux/artifacts/* input_artifacts || true

python tools/run_tests/task_runner.py -j 4 \
    -f distribtest $language $curr_platform $architecture \
    $@
