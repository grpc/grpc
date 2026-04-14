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

# configure ccache
source tools/internal_ci/helper_scripts/prepare_ccache_rc

# Example for Debian/Ubuntu
sudo apt-get update
sudo apt-get install -y redis-tools

echo "--- Inspecting Redis Database ---"
# Basic connection test
redis-cli -h 10.76.145.84 -p 6379 ping

# Get general information and stats
redis-cli -h 10.76.145.84 -p 6379 info

# Get database size
redis-cli -h 10.76.145.84 -p 6379 dbsize

# List some keys (Avoid KEYS * in production on large databases)
# Use SCAN for better performance on live instances
redis-cli -h 10.76.145.84 -p 6379 scan 0 count 100

# Get memory usage for a specific key (if you know one)
# redis-cli -h 10.76.145.84 -p 6379 memory usage YOUR_KEY

# To get the type of a key
# redis-cli -h 10.76.145.84 -p 6379 type YOUR_KEY

# To get the value of a String key
# redis-cli -h 10.76.145.84 -p 6379 get YOUR_KEY

# To get elements from a List key
# redis-cli -h 10.76.145.84 -p 6379 lrange YOUR_KEY 0 -1
echo "--- End of Redis Inspection ---"

exit 1

# Build all python linux artifacts (this step actually builds all the binary wheels and source archives)
tools/run_tests/task_runner.py -f artifact linux python ${TASK_RUNNER_EXTRA_FILTERS} -j 12 -x build_artifacts/sponge_log.xml || BUILD_ARTIFACT_FAILED="true"

# the next step expects to find the artifacts from the previous step in the "input_artifacts" folder.
rm -rf input_artifacts
mkdir -p input_artifacts
cp -r artifacts/* input_artifacts/ || true

# This step simply collects python artifacts from subdirectories of input_artifacts/ and copies them to artifacts/

# PythonPackage targets do not support the `presubmit` label.
# For this reason we remove `presubmit` label selector from TASK_RUNNER_EXTRA_FILTERS,
# which looks like TASK_RUNNER_EXTRA_FILTERS="presubmit -e aarch64 musllinux_1_2"
# for a presubmit with an exclude filter.
PACKAGE_TASK_RUNNER_EXTRA_FILTERS="${TASK_RUNNER_EXTRA_FILTERS//presubmit /}"

tools/run_tests/task_runner.py -f package linux python ${PACKAGE_TASK_RUNNER_EXTRA_FILTERS} -x build_packages/sponge_log.xml || FAILED="true"

# the next step expects to find the artifacts from the previous step in the "input_artifacts" folder.
# in addition to that, preserve the contents of "artifacts" directory since we want kokoro
# to upload its contents as job output artifacts
rm -rf input_artifacts
mkdir -p input_artifacts
cp -r artifacts/* input_artifacts/ || true

# exit early if build_artifact or package task fails, for faster test run time
if [[ "$BUILD_ARTIFACT_FAILED" != "" || "$FAILED" != "" ]]; then
  exit 1
fi

# Run all python linux distribtests

tools/run_tests/task_runner.py -f distribtest linux python ${TASK_RUNNER_EXTRA_FILTERS} -j 12 -x distribtests/sponge_log.xml || FAILED="true"

# This step checks if any of the artifacts exceeds a per-file size limit.
tools/internal_ci/helper_scripts/check_python_artifacts_size.sh

tools/internal_ci/helper_scripts/store_artifacts_from_moved_src_tree.sh

if [ "$FAILED" != "" ]
then
  exit 1
fi
