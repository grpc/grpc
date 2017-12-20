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

# If this is a PR using RUN_TESTS_FLAGS var, then add flags to filter tests
if [ -n "$KOKORO_GITHUB_PULL_REQUEST_NUMBER" ] && [ -n "$RUN_TESTS_FLAGS" ]; then
  sudo apt-get install -y jq
  ghprbTargetBranch=$(curl -s https://api.github.com/repos/grpc/grpc/pulls/$KOKORO_GITHUB_PULL_REQUEST_NUMBER | jq -r .base.ref)
  export RUN_TESTS_FLAGS="$RUN_TESTS_FLAGS --filter_pr_tests --base_branch origin/$ghprbTargetBranch"
fi

tools/run_tests/run_tests_matrix.py $RUN_TESTS_FLAGS || FAILED="true"

# Reveal leftover processes that might be left behind by the build
ps aux | grep -i kbuilder

echo 'Exiting gRPC main test script.'

if [ "$FAILED" != "" ]
then
  exit 1
fi
