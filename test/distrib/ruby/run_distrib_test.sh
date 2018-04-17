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

set -ex

cd "$(dirname "$0")"

ARCH=$1
PLATFORM=$2
# Create an indexed local gem source with gRPC gems to test
GEM_SOURCE=../../../gem_source
mkdir -p "${GEM_SOURCE}/gems"
cp "$EXTERNAL_GIT_ROOT"/input_artifacts/grpc-*"$ARCH-$PLATFORM".gem "${GEM_SOURCE}/gems"
# TODO: rewrite the following line to be shellcheck-compliant
# shellcheck disable=SC2010
if [[ "$(ls "${GEM_SOURCE}/gems" | grep -c grpc)" != 1 ]]; then
  echo "Sanity check failed. Copied over more than one grpc gem into the gem source directory."
  exit 1
fi;
gem install builder
gem generate_index --directory "${GEM_SOURCE}"

bundle install

bundle exec ./distribtest.rb

# Attempt to repro https://github.com/google/protobuf/issues/4210.
# TODO: This sanity check only works for linux-based distrib tests and for
# binary gRPC packages. It will need to be ran conditionally if this test script is
# used for other types of distrib tests.
INSTALLATION_DIR="$(gem env | grep '\- INSTALLATION DIRECTORY' | awk '{ print $4 }')"
if [[ "$(find "$INSTALLATION_DIR" -name 'grpc_c.so' | wc -l)" == 0 ]]; then
  echo "Sanity check failed. The gRPC package is not installed in $INSTALLATION_DIR."
  exit 1
fi
LIBRUBY_DEPENDENCY_EXISTS="$(find "$INSTALLATION_DIR" -name 'grpc_c.so' -exec ldd {} \; | grep -c 'libruby')" || true
if [[ "$LIBRUBY_DEPENDENCY_EXISTS" != 0 ]]; then
  echo "A grpc_c.so file in this binary gRPC package is dynamically linked to libruby."
fi
DEPENDENCY_NOT_FOUND="$(find "$INSTALLATION_DIR" -name 'grpc_c.so' -exec ldd {} \; | grep -c 'not found')" || true
if [[ "$DEPENDENCY_NOT_FOUND" != 0 ]]; then
  echo "A grpc_c.so file in this binary gRPC package has an non-portable dependency."
fi
if [ "$LIBRUBY_DEPENDENCY_EXISTS" != 0 ] || [ "$DEPENDENCY_NOT_FOUND" != 0 ]; then
  exit 1
fi
