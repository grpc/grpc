#!/bin/bash
# Copyright 2022 The gRPC Authors
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

export PREPARE_BUILD_INSTALL_DEPS_RUBY=true
source tools/internal_ci/helper_scripts/prepare_build_macos_rc

# TODO(jtattermusch): can some of these steps be removed?
# needed to build ruby artifacts
gem install rubygems-update
update_rubygems
time bash tools/distrib/build_ruby_environment_macos.sh

# Build all ruby macos artifacts (this step actually builds all the native and source gems)
tools/run_tests/task_runner.py -f artifact macos ruby ${TASK_RUNNER_EXTRA_FILTERS} -j 4 -x build_artifacts/sponge_log.xml || FAILED="true"

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

# TODO(jtattermusch): Here we would normally run ruby macos distribtests, but currently no such tests are defined
# in distribtest_targets.py

tools/internal_ci/helper_scripts/store_artifacts_from_moved_src_tree.sh

if [ "$FAILED" != "" ]
then
  exit 1
fi
