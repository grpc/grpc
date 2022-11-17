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

set -ex

cd "$(dirname "$0")"

cp -r "$EXTERNAL_GIT_ROOT"/input_artifacts/grpc-*.tgz .

# get name of the PHP package archive to test (we don't know
# the exact version string in advance)
GRPC_PEAR_PACKAGE_NAME=$(find . -regex '.*/grpc-[0-9].*.tgz' | sed 's|./||')

# Use -j4 since higher parallelism can lead to "resource unavailable"
# errors during the build. See b/257261061#comment4
sudo MAKEFLAGS=-j4 pecl install "${GRPC_PEAR_PACKAGE_NAME}"

php -d extension=grpc.so -d max_execution_time=300 distribtest.php
