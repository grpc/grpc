#!/usr/bin/env bash
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
#
# This script is invoked by Jenkins and runs a diff on the microbenchmarks
set -ex

# avoid slow finalization after the script has exited.
source $(dirname $0)/../../../tools/internal_ci/helper_scripts/move_src_tree_and_respawn_itself_rc

# List of benchmarks that provide good signal for analyzing performance changes in pull requests

# Enter the gRPC repo root
cd $(dirname $0)/../../..

export PREPARE_BUILD_INSTALL_DEPS_OBJC=true
source tools/internal_ci/helper_scripts/prepare_build_macos_rc

if [ "${KOKORO_GITHUB_PULL_REQUEST_TARGET_BRANCH}" != "" ]
then
  # running on PR, generate size diff
  DIFF_BASE="origin/${KOKORO_GITHUB_PULL_REQUEST_TARGET_BRANCH}"
else
  # running as continous build, only generate numbers for the current revision
  DIFF_BASE=""
fi

tools/profiling/ios_bin/binary_size.py \
  --diff_base="${DIFF_BASE}"
