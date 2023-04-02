#!/usr/bin/env bash
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

# Enter the gRPC repo root
cd $(dirname $0)/../../..

# some extra pip packages are needed for the check_on_pr.py script to work
# TODO(jtattermusch): avoid needing to install these pip packages each time
time python3 -m pip install --user -r tools/internal_ci/helper_scripts/requirements.linux_perf.txt

tools/run_tests/start_port_server.py

tools/internal_ci/linux/run_if_c_cpp_modified.sh tools/profiling/memory/memory_diff.py \
  -d "origin/$KOKORO_GITHUB_PULL_REQUEST_TARGET_BRANCH"
