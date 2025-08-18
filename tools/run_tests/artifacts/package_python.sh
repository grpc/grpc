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

set -eux

cd "$(dirname "$0")/../../.."

mkdir -p artifacts/

# All the python packages have been built in the artifact phase already
# and we only collect them here to deliver them to the distribtest phase.

find "${EXTERNAL_GIT_ROOT}/input_artifacts/" | sed -e "s/[^-][^\/]*\// |/g" -e "s/|\([^ ]\)/|-\1/"

# Build the find command to include all files which start with
# ARTIFACT_PREFIX but does not match any of the EXCLUDE_PATTERNS
find_cmd=(
    find "${EXTERNAL_GIT_ROOT}/input_artifacts/"
    -maxdepth 1
    -type d
    -name "${ARTIFACT_PREFIX}*"
)

# Loop through the exclusion patterns and add them to the command array
if [[ -n "$EXCLUDE_PATTERNS" ]]; then
    for pattern in $EXCLUDE_PATTERNS; do
        find_cmd+=(-not -name "$pattern")
    done
fi


# Copy all files except '*.tar.gz' and '*py3-none-any.whl' files.
"${find_cmd[@]}" -print0 \
    | xargs -0 -I% find % -type f -maxdepth 1 \
    -not -name "*.tar.gz" -not -name "*py3-none-any.whl" \
    -exec cp -v {} ./artifacts \;
