#!/bin/bash
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

set -ex

# avoid slow finalization after the script has exited.
source $(dirname $0)/../../../tools/internal_ci/helper_scripts/move_src_tree_and_respawn_itself_rc

# change to grpc repo root
cd $(dirname $0)/../../..

source tools/internal_ci/helper_scripts/prepare_build_linux_rc

# Build all PHP linux artifacts
tools/run_tests/task_runner.py -f artifact linux php ${TASK_RUNNER_EXTRA_FILTERS} -j 2 -x build_artifacts/sponge_log.xml || FAILED="true"

# the next step expects to find the artifacts from the previous step in the "input_artifacts" folder.
rm -rf input_artifacts
mkdir -p input_artifacts
cp -r artifacts/* input_artifacts/ || true

# This step simply collects php artifacts from subdirectories of input_artifacts/ and copies them to artifacts/
tools/run_tests/task_runner.py -f package linux php -x build_packages/sponge_log.xml || FAILED="true"

# the next step expects to find the artifacts from the previous step in the "input_artifacts" folder.
# in addition to that, preserve the contents of "artifacts" directory since we want kokoro
# to upload its contents as job output artifacts
rm -rf input_artifacts
mkdir -p input_artifacts
cp -r artifacts/* input_artifacts/ || true

# Run all PHP linux distribtests
# We run the distribtests even if some of the artifacts have failed to build, since that gives
# a better signal about which distribtest are affected by the currently broken artifact builds.
tools/run_tests/task_runner.py -f distribtest linux php ${TASK_RUNNER_EXTRA_FILTERS} -j 2 -x distribtests/sponge_log.xml || FAILED="true"

tools/internal_ci/helper_scripts/store_artifacts_from_moved_src_tree.sh

if [ "$FAILED" != "" ]
then
  exit 1
fi
