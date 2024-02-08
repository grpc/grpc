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

# List all input artifacts we obtained for easier troubleshooting.
ls -lR input_artifacts

# Put the input packages where the legacy logic for running
# Python distribtest expects to find them.
# See distribtest_targets.py for details.
# TODO(jtattermusch): get rid of the manual renames of artifact files.
export EXTERNAL_GIT_ROOT="$(pwd)"
mv input_artifacts/package_python_linux/* input_artifacts/ || true

test/distrib/python/run_binary_distrib_test.sh
