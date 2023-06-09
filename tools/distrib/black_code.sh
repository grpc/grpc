#!/bin/bash
# Copyright 2015 gRPC authors.
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

ACTION="${1:-}"
[[ $ACTION == '' ]] || [[ $ACTION == '--diff' ]] || [[ $ACTION == '--check' ]]

# change to root directory
cd "$(dirname "${0}")/../.."

DIRS=(
    'examples'
    'src'
    'test'
    'tools'
    'setup.py'
)

VIRTUALENV=black_virtual_environment

python3 -m virtualenv $VIRTUALENV -p $(which python3)
PYTHON=${VIRTUALENV}/bin/python
"$PYTHON" -m pip install black==23.3.0

$PYTHON -m black $ACTION "${DIRS[@]}"
