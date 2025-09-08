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

# Build the find command to include all files which start with ARTIFACT_PREFIX
find_cmd=(
    find "${EXTERNAL_GIT_ROOT}/input_artifacts/"
    -maxdepth 1
    -type d
    -name "${ARTIFACT_PREFIX}*"
)


# Copy only the '*.tar.gz' and '*py3-none-any.whl' files.
"${find_cmd[@]}" -print0 | xargs -0 -I% \
    find % \( -name "*.tar.gz" -o -name "*py3-none-any.whl" \) \
    -exec cp -v {} ./artifacts \;
