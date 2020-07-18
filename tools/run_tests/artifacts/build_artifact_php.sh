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

# build the PHP extension archive (this just zips all the files up)
pear package
# test installing the PHP extension via pecl install
find . -name "grpc-*.tgz" | cut -b3- | xargs pecl install
# verified that the grpc extension is installed
php -d extension=grpc.so --re grpc | head -1

cp -r grpc-*.tgz "${ARTIFACTS_OUT}"/
