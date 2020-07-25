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

set -ex

cd "$(dirname "$0")/../../.."

mkdir -p "${ARTIFACTS_OUT}"

# Clean up the source files manifest. Some renaming needs to be done first.
./src/php/bin/prepare_pecl_extension.sh
# Build the PHP extension archive (this just zips all the files up)
pear package
# Note: the extension compiled by this step is not being used in any
# way, i.e. they are not the pacakge being distributed.
# This is done here to get an early signal for compiling the PHP
# extension in some form.
find . -name "grpc-*.tgz" | cut -b3- | xargs pecl install
# Verified that the grpc extension is built properly.
php -d extension=grpc.so --re grpc | head -1

cp -r grpc-*.tgz "${ARTIFACTS_OUT}"/
