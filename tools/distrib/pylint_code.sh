#!/bin/bash
# Copyright 2017 The gRPC Authors
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

TEST_DIRS=(
    'src/python/grpcio_tests/tests'
)

VIRTUALENV=python_pylint_venv
python -m virtualenv $VIRTUALENV

PYTHON=$VIRTUALENV/bin/python

$PYTHON -m pip install --upgrade pip==10.0.1
$PYTHON -m pip install pylint==1.9.2

EXIT=0
for dir in "${DIRS[@]}"; do
  $PYTHON -m pylint --rcfile=.pylintrc -rn "$dir" || EXIT=1
done

for dir in "${TEST_DIRS[@]}"; do
  $PYTHON -m pylint --rcfile=.pylintrc-tests -rn "$dir" || EXIT=1
done

exit $EXIT
