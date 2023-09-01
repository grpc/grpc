#!/usr/bin/env bash
# Copyright 2021 The gRPC authors.
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

# TODO(jtattermusch): make build work with bazel 2.2.0 and bazel 1.2.1 if that's reasonably simple.
VERSIONS=$(cat ../../../bazel/supported_versions.txt)

FAILED_VERSIONS=""
for VERSION in $VERSIONS; do
    echo "Running bazel distribtest with bazel version ${VERSION}"
    ./test_single_bazel_version.sh "${VERSION}" || FAILED_VERSIONS="${FAILED_VERSIONS}${VERSION} "
done

if [ "$FAILED_VERSIONS" != "" ]
then
  echo "Bazel distribtest failed: Failing versions: ${FAILED_VERSIONS}"
  exit 1
fi
