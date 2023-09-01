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

# prerequisites for ruby artifact build on linux
source tools/internal_ci/helper_scripts/prepare_build_linux_ruby_artifact_rc

# configure ccache
source tools/internal_ci/helper_scripts/prepare_ccache_rc

# Build all ruby linux artifacts (this step actually builds all the native and source gems)
tools/run_tests/task_runner.py -f artifact linux ruby ${TASK_RUNNER_EXTRA_FILTERS} -j 6 --inner_jobs 6 -x build_artifacts/sponge_log.xml || FAILED="true"

# Ruby "build_package" step is basically just a passthough for the "grpc" gems, so it's enough to just
# copy the native gems directly to the "distribtests" step and skip the "build_package" phase entirely.
# Note that by skipping the "build_package" step, we are also skipping the build of "grpc-tools" gem
# but that's fine since the distribtests only test the "grpc" native gems.

# The next step expects to find the artifacts from the previous step in the "input_artifacts" folder.
# in addition to that, preserve the contents of "artifacts" directory since we want kokoro
# to upload its contents as job output artifacts.
rm -rf input_artifacts
mkdir -p input_artifacts
cp -r artifacts/ruby_native_gem_*/* input_artifacts/ || true
# Also copy the gems directly to the "artifacts" directory, but do that without invoking ruby's "build_package"
# phase.
cp -r artifacts/ruby_native_gem_*/* artifacts/ || true

# Run all ruby linux distribtests
# We run the distribtests even if some of the artifacts have failed to build, since that gives
# a better signal about which distribtest are affected by the currently broken artifact builds.
tools/run_tests/task_runner.py -f distribtest linux ruby ${TASK_RUNNER_EXTRA_FILTERS} -j 12 -x distribtests/sponge_log.xml || FAILED="true"

tools/internal_ci/helper_scripts/store_artifacts_from_moved_src_tree.sh

if [ "$FAILED" != "" ]
then
  exit 1
fi
