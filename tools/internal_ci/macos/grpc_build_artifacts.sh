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

# avoid slow finalization after the script has exited.
source $(dirname $0)/../../../tools/internal_ci/helper_scripts/move_src_tree_and_respawn_itself_rc

# change to grpc repo root
cd $(dirname $0)/../../..

export PREPARE_BUILD_INSTALL_DEPS_CSHARP=true
export PREPARE_BUILD_INSTALL_DEPS_PYTHON=true
export PREPARE_BUILD_INSTALL_DEPS_PHP=true
source tools/internal_ci/helper_scripts/prepare_build_macos_rc

# install cython for all python versions
python2.7 -m pip install -U cython setuptools==44.1.1 wheel --user
python3.5 -m pip install -U cython setuptools==44.1.1 wheel --user
python3.6 -m pip install -U cython setuptools==44.1.1 wheel --user
python3.7 -m pip install -U cython setuptools==44.1.1 wheel --user
python3.8 -m pip install -U cython setuptools==44.1.1 wheel --user
python3.9 -m pip install -U cython setuptools==44.1.1 wheel --user

# try speeding up builds with ccache
brew install ccache || true
export PATH="/usr/local/opt/ccache/libexec:$PATH"

tools/run_tests/task_runner.py -f artifact macos python3.6 ${TASK_RUNNER_EXTRA_FILTERS} -j 2 --inner_jobs 4 || FAILED="true"

# useful for seeing is ccache was helpful
ccache --show-stats || true
ccache -z || true


tools/run_tests/task_runner.py -f artifact macos python3.10 ${TASK_RUNNER_EXTRA_FILTERS} -j 2 --inner_jobs 4 || FAILED="true"

# useful for seeing is ccache was helpful
ccache --show-stats || true
ccache -z || true

tools/internal_ci/helper_scripts/store_artifacts_from_moved_src_tree.sh



if [ "$FAILED" != "" ]
then
  exit 1
fi
