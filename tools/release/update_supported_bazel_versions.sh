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

# This script can be used to manually update the specific versions of Bazel
# tested by the Bazel distribtests and advertised as supported in documentation.
# This will update the supported_versions.txt file, which will be templated into
# the bazel_support.md document.

# This script selects the latest patch release of the two most recent major
# versions of Bazel. If you want to include other versions in the set of
# supported versions, then you will need to manually edit the ifle.

set -xeuo pipefail

cd "$(dirname "$0")/../.."

# The number of most recent supported major Bazel versions.
SUPPORT_RANGE="2"

# Retrieve all git tags from the Bazel git repo.
TAGS=$(git ls-remote --tags git@github.com:bazelbuild/bazel.git | awk '{print $2;}' | sed 's|refs/tags/||g')

# Find the n most recent major versions.
MAJOR_VERSIONS=$(echo "$TAGS" | egrep '^[0-9]+\.[0-9]+\.[0-9]+$' | cut -d'.' -f1 | sort -r | uniq | head -n"$SUPPORT_RANGE")

SUPPORTED_VERSIONS=""

# For each major version selected, find the most recent patch release.
while read -r MAJOR_VERSION; do
  LATEST_PATCH=$(echo "$TAGS" | egrep "^${MAJOR_VERSION}\.[0-9]+\.[0-9]+$" | sort -nr | head -n1)
  SUPPORTED_VERSIONS="$SUPPORTED_VERSIONS$LATEST_PATCH\n"
done<<<"$MAJOR_VERSIONS"

printf "$SUPPORTED_VERSIONS" | tee bazel/supported_versions.txt
