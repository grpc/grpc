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

export PREPARE_BUILD_INSTALL_DEPS_PYTHON=true
source tools/internal_ci/helper_scripts/prepare_build_macos_rc

# TODO(jtattermusch): cleanup this prepare build step (needed for python artifact build)
# install cython for all python versions
python3.7 -m pip install -U 'cython<3.0.0rc1' setuptools==65.4.1 wheel --user
python3.8 -m pip install -U 'cython<3.0.0rc1' setuptools==65.4.1 wheel --user
python3.9 -m pip install -U 'cython<3.0.0rc1' setuptools==65.4.1 wheel --user
python3.10 -m pip install -U 'cython<3.0.0rc1' setuptools==65.4.1 wheel --user
python3.11 -m pip install -U 'cython<3.0.0rc1' setuptools==65.4.1 wheel --user

# Build all python macos artifacts (this step actually builds all the binary wheels and source archives)
tools/run_tests/task_runner.py -f artifact macos python ${TASK_RUNNER_EXTRA_FILTERS} -j 4 -x build_artifacts/sponge_log.xml || FAILED="true"

# the next step expects to find the artifacts from the previous step in the "input_artifacts" folder.
rm -rf input_artifacts
mkdir -p input_artifacts
cp -r artifacts/* input_artifacts/ || true

# Collect the python artifact from subdirectories of input_artifacts/ to artifacts/
# TODO(jtattermusch): when collecting the artifacts that will later be uploaded as kokoro job artifacts,
# potentially skip some file names that would clash with linux-created artifacts.
cp -r input_artifacts/python_*/* artifacts/ || true

# TODO(jtattermusch): Here we would normally run python macos distribtests, but currently no such tests are defined
# in distribtest_targets.py

tools/internal_ci/helper_scripts/store_artifacts_from_moved_src_tree.sh

if [ "$FAILED" != "" ]
then
  exit 1
fi
