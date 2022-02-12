#!/usr/bin/env bash
# Copyright 2021 The gRPC Authors
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

# If you have used "move_src_tree_and_respawn_itself_rc" in a CI script
# and the script produces artifacts to be stored by kokoro, run this
# at the end of the script.

set -ex

if [ "${GRPC_TEST_REPORT_BASE_DIR}" == "" ]
then
  # looks like the move_src_tree_and_respawn_itself_rc hasn't been used
  # and we're not running under a moved source tree
  exit 0
fi

# change to grpc repo root
cd "$(dirname "$0")/../../.."

# If running under a moved source tree (see grpc/tools/internal_ci/helper_scripts/move_src_tree_and_respawn_itself_rc),
# we need to copy the artifacts produced by the build to a location that's stored by kokoro.

# artifacts in this directory will be stored by kokoro
mkdir -p "${GRPC_TEST_REPORT_BASE_DIR}/artifacts"
time cp -r artifacts/* "${GRPC_TEST_REPORT_BASE_DIR}/artifacts" || true
