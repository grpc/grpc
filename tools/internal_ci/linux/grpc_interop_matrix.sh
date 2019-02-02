#!/usr/bin/env bash
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

export LANG=en_US.UTF-8

# Enter the gRPC repo root
cd $(dirname $0)/../../..

source tools/internal_ci/helper_scripts/prepare_build_linux_rc

# TODO(jtattermusch): Diagnose out of disk space problems. Remove once not needed.
df -h

tools/interop_matrix/run_interop_matrix_tests.py $RUN_TESTS_FLAGS || FAILED="true"

# TODO(jtattermusch): Diagnose out of disk space problems. Remove once not needed.
df -h

# TODO(jtattermusch): Diagnose problems with gcloud auth. Remove once not needed.
gcloud info
gcloud info --show-log

if [ "$FAILED" != "" ]
then
  exit 1
fi
