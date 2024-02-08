#!/bin/bash
# Copyright 2023 The gRPC Authors
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

mkdir -p artifacts

# List all input artifacts we obtained for easier troubleshooting.
ls -lR input_artifacts

# All the python packages have been built in the artifact phase already
# and we only collect them here to deliver them to the distribtest phase.
# This is the same logic as in "tools/run_tests/artifacts/build_package_python.sh",
# but expects different layout under input_artifacts.
cp -r input_artifacts/artifact_python_*/* artifacts/ || true
