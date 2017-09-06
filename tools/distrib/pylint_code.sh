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

set -ex

# change to root directory
cd "$(dirname "$0")/../.."

DIRS=(
    'src/python/grpcio/grpc'
    'src/python/grpcio_health_checking/grpc_health'
    'src/python/grpcio_reflection/grpc_reflection'
    'src/python/grpcio_testing/grpc_testing'
)

VIRTUALENV=python_pylint_venv

virtualenv $VIRTUALENV
PYTHON=$(realpath $VIRTUALENV/bin/python)
$PYTHON -m pip install --upgrade pip
$PYTHON -m pip install pylint==1.6.5

for dir in "${DIRS[@]}"; do
  $PYTHON -m pylint --rcfile=.pylintrc -rn "$dir" || exit $?
done

exit 0
