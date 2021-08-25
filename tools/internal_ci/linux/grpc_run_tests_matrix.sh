#!/bin/bash
# Copyright 2017 gRPC authors.
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

# change to grpc repo root
cd $(dirname $0)/../../..

source tools/internal_ci/helper_scripts/prepare_build_linux_rc

echo "Env variables provided by kokoro:"
echo "KOKORO_GIT_COMMIT $KOKORO_GIT_COMMIT"
echo "KOKORO_GITHUB_PULL_REQUEST_COMMIT $KOKORO_GITHUB_PULL_REQUEST_COMMIT"
echo "KOKORO_GITHUB_PULL_REQUEST_NUMBER $KOKORO_GITHUB_PULL_REQUEST_NUMBER"
echo "KOKORO_GITHUB_COMMIT $KOKORO_GITHUB_COMMIT"

# show checked-out version
echo "Currently checked out commit:"
git log -2

exit 1
