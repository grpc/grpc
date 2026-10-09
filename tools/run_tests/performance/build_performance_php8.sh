#!/bin/bash
# Copyright 2017 gRPC authors.
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

set -ex

cd "$(dirname "$0")/../../.."
root="$(pwd)"
CONFIG=${CONFIG:-opt}
python tools/run_tests/run_tests.py -l php8 -c "$CONFIG" --build_only -j 8

# Set up all dependencies needed for PHP QPS test
cd src/php/tests/qps
composer install
# Install protobuf C-extension for php
cd "${root}/third_party/protobuf/php/ext/google/protobuf"

# The PHP extension's build configuration (config.m4) expects 'utf8_range' 
# to exist locally within the extension tree rather than the repository root.
# Copy the dependency from the Protobuf third_party submodule to satisfy this.
mkdir -p third_party/utf8_range
cp -r "${root}/third_party/utf8_range/"* third_party/utf8_range/

phpize
./configure
make -j8

# Prepare for ruby proxy workers
cd "${root}"
python tools/run_tests/run_tests.py -l ruby -c "$CONFIG" --build_only -j 8

