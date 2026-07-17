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

PS4='+ $(date "+[%H:%M:%S %Z]")\011 '
set -ex

cd "$(dirname "$0")"

cp -r "$EXTERNAL_GIT_ROOT"/input_artifacts/grpc-*.tgz .

# get name of the PHP package archive to test (we don't know
# the exact version string in advance)
GRPC_PEAR_PACKAGE_NAME=$(find . -regex '.*/grpc-[0-9].*.tgz' | sed 's|./||')

PHP_VERSION="${1:-8.2}"
PHP_PATH="/usr/local/opt/php@${PHP_VERSION}"
if [ ! -d "${PHP_PATH}" ]; then
  PHP_PATH="/opt/homebrew/opt/php@${PHP_VERSION}"
fi

if [ -d "${PHP_PATH}" ]; then
  PECL_BIN="${PHP_PATH}/bin/pecl"
  PHP_BIN="${PHP_PATH}/bin/php"
else
  PECL_BIN="pecl"
  PHP_BIN="php"
fi

# Use -j4 since higher parallelism can lead to "resource unavailable"
# errors during the build. See b/257261061#comment4
sudo env PATH="$PATH" MAKEFLAGS=-j4 "${PECL_BIN}" install "${GRPC_PEAR_PACKAGE_NAME}"

"${PHP_BIN}" -d extension=grpc.so -d max_execution_time=300 distribtest.php
