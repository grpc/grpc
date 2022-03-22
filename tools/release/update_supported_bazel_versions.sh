#!/bin/bash
#Copyright 2022 The gRPC authors.
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

set -xeuo pipefail

cd "$(dirname "$0")/../.."

SUPPORT_RANGE="2"

TAGS=$(git ls-remote --tags git@github.com:bazelbuild/bazel.git | awk '{print $2;}' | sed 's|refs/tags/||g')

MAJOR_VERSIONS=$(echo "$TAGS" | egrep '^[0-9]+\.[0-9]+\.[0-9]+$' | cut -d'.' -f1 | sort -r | uniq | head -n"$SUPPORT_RANGE")

SUPPORTED_VERSIONS=""

while read -r MAJOR_VERSION; do
  LATEST_PATCH=$(echo "$TAGS" | egrep "^${MAJOR_VERSION}\.[0-9]+\.[0-9]+$" | sort -nr | head -n1)
  SUPPORTED_VERSIONS="$SUPPORTED_VERSIONS$LATEST_PATCH\n"
done<<<"$MAJOR_VERSIONS"

printf "$SUPPORTED_VERSIONS" | tee test/distrib/bazel/supported_versions.txt
