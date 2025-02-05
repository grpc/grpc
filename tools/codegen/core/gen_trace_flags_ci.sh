#!/bin/bash
# Copyright 2024 The gRPC authors.
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
# cd to repo root
dir=$(dirname "${0}")
cd "${dir}/../../.."

VIRTUALENV=venv_gen_trace_flags_ci
python3 -m virtualenv $VIRTUALENV
source $VIRTUALENV/bin/activate

python3 -m pip install absl-py
python3 ${dir}/gen_trace_flags.py --check
