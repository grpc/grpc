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

# Source this rc script to prepare the environment for linux perf builds

# Need to increase open files limit and size for perf test
ulimit -n 32768
ulimit -c unlimited

python3 -m pip install pip==19.3.1

# Python dependencies for tools/run_tests/python_utils/check_on_pr.py
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
time python3 -m pip install --user -r $DIR/requirements.linux_perf.txt

git submodule update --init
