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

# change to grpc repo root
cd $(dirname $0)/../../..

source tools/internal_ci/helper_scripts/prepare_build_linux_rc

# some distribtests use a pre-registered binfmt_misc hook
# to automatically execute foreign binaries (such as aarch64)
# under qemu emulator.
source tools/internal_ci/helper_scripts/prepare_qemu_rc

# Build all python linux artifacts (this step actually builds all the binary wheels and source archives)
tools/run_tests/task_runner.py -f artifact linux python -j 6 -x build_artifacts/sponge_log.xml || FAILED="true"

# the next step expects to find the artifacts from the previous step in the "input_artifacts" folder.
rm -rf input_artifacts
mkdir -p input_artifacts
cp -r artifacts/* input_artifacts/ || true
rm -rf artifacts_from_build_artifacts_step
mv artifacts artifacts_from_build_artifacts_step || true

# This step mostly just copies artifacts from input_artifacts (but it also does some wheel stripping)
tools/run_tests/task_runner.py -f package linux python -x build_packages/sponge_log.xml || FAILED="true"

# the next step expects to find the artifacts from the previous step in the "input_artifacts" folder.
# in addition to that, preserve the contents of "artifacts" directory since we want kokoro
# to upload its contents as job output artifacts
rm -rf input_artifacts
mkdir -p input_artifacts
cp -r artifacts/* input_artifacts/ || true

# Run all python linux distribtests
# We run the distribtests even if some of the artifacts have failed to build, since that gives
# a better signal about which distribtest are affected by the currently broken artifact builds.
tools/run_tests/task_runner.py -f distribtest linux python -j 6 -x distribtests/sponge_log.xml || FAILED="true"

if [ "$FAILED" != "" ]
then
  exit 1
fi
