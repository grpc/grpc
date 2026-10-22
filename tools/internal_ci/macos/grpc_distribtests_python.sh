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
# Pre-install the tools that build_artifact_python.sh installs, with the same
# pins but with retries on transient PyPI failures. Its own pip installs then
# find them already satisfied instead of hitting PyPI without retries.
# Keep the package list in sync with build_artifact_python.sh.
retry_with_backoff python3.10 -m pip install --user --upgrade pip
retry_with_backoff python3.10 -m pip install --user setuptools==77.0.1 wheel==0.43.0 build==1.5.0 'cython==3.2.8'
retry_with_backoff python3.11 -m pip install --user --upgrade pip
retry_with_backoff python3.11 -m pip install --user setuptools==77.0.1 wheel==0.43.0 build==1.5.0 'cython==3.2.8'
retry_with_backoff python3.12 -m pip install --user --break-system-packages --upgrade pip
retry_with_backoff python3.12 -m pip install --user --break-system-packages setuptools==77.0.1 wheel==0.43.0 build==1.5.0 'cython==3.2.8'
retry_with_backoff python3.13 -m pip install --user --break-system-packages --upgrade pip
retry_with_backoff python3.13 -m pip install --user --break-system-packages setuptools==77.0.1 wheel==0.43.0 build==1.5.0 'cython==3.2.8'
retry_with_backoff python3.14 -m pip install --user --break-system-packages --upgrade pip
retry_with_backoff python3.14 -m pip install --user --break-system-packages setuptools==77.0.1 wheel==0.43.0 build==1.5.0 'cython==3.2.8'
retry_with_backoff python3.15 -m pip install --user --break-system-packages --upgrade pip
retry_with_backoff python3.15 -m pip install --user --break-system-packages setuptools==77.0.1 wheel==0.43.0 build==1.5.0 'cython==3.2.8'

# Build all python macos artifacts (this step actually builds all the binary wheels and source archives)
tools/run_tests/task_runner.py -f artifact macos python ${TASK_RUNNER_EXTRA_FILTERS} -j 2 -x build_artifacts/sponge_log.xml || FAILED="true"

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

# This step checks if any of the artifacts exceeds a per-file size limit.
tools/internal_ci/helper_scripts/check_python_artifacts_size.sh

tools/internal_ci/helper_scripts/store_artifacts_from_moved_src_tree.sh

if [ "$FAILED" != "" ]
then
  exit 1
fi
