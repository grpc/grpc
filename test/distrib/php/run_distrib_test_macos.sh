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

# For PHP 8.5, Homebrew formula is 'php' (unversioned) instead of 'php@8.5'
if [ "${PHP_VERSION}" == "8.5" ] && [ ! -d "${PHP_PATH}" ]; then
  if [ -d "/usr/local/opt/php" ]; then
    PHP_PATH="/usr/local/opt/php"
  elif [ -d "/opt/homebrew/opt/php" ]; then
    PHP_PATH="/opt/homebrew/opt/php"
  fi
fi

if [ -d "${PHP_PATH}" ]; then
  PECL_BIN="${PHP_PATH}/bin/pecl"
  PHP_BIN="${PHP_PATH}/bin/php"
  export PATH="${PHP_PATH}/bin:${PATH}"
else
  PECL_BIN="pecl"
  PHP_BIN="php"
fi

# Force PECL to use resolved, version-unique /private/tmp directory to avoid macOS symlink path mismatch
# and race conditions when multiple PHP distribtests run concurrently (-j 4).
PECL_TEMP_DIR="/private/tmp/pear/temp_${PHP_VERSION}_$$"
sudo mkdir -p "${PECL_TEMP_DIR}"

PEAR_CONF="${PECL_TEMP_DIR}/pear.conf"
sudo "${PECL_BIN}" config-create "${PECL_TEMP_DIR}" "${PEAR_CONF}"

# Update temp_dir, download_dir, and cache_dir inside pear.conf
sudo "${PHP_BIN}" -r '
$f = "'"${PEAR_CONF}"'";
$dir = "'"${PECL_TEMP_DIR}"'";
$c = unserialize(file_get_contents($f));
if (is_array($c)) {
    $c["temp_dir"] = $dir;
    $c["download_dir"] = $dir;
    $c["cache_dir"] = $dir;
    file_put_contents($f, serialize($c));
}
'

# Use -j4 since higher parallelism can lead to "resource unavailable"
# errors during the build. See b/257261061#comment4
sudo env PATH="$PATH" \
  PHP_PEAR_TEMP_DIR="${PECL_TEMP_DIR}" \
  PHP_PEAR_DOWNLOAD_DIR="${PECL_TEMP_DIR}" \
  PHP_PEAR_CACHE_DIR="${PECL_TEMP_DIR}" \
  MAKEFLAGS=-j4 "${PECL_BIN}" -c "${PEAR_CONF}" -d temp_dir="${PECL_TEMP_DIR}" -d download_dir="${PECL_TEMP_DIR}" -d cache_dir="${PECL_TEMP_DIR}" install "${GRPC_PEAR_PACKAGE_NAME}"
sudo rm -rf "${PECL_TEMP_DIR}"

"${PHP_BIN}" -d extension=grpc.so -d max_execution_time=300 distribtest.php
