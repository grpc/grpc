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

# NOTE(rbellevi): We ignore generated code.
IGNORE_PATTERNS=--ignore-patterns='.*pb2\.py,.*pb2_grpc\.py'

# change to root directory
cd "$(dirname "$0")/../.."

DIRS=(
    'src/python/grpcio/grpc'
    'src/python/grpcio_channelz/grpc_channelz'
    'src/python/grpcio_health_checking/grpc_health'
    'src/python/grpcio_reflection/grpc_reflection'
    'src/python/grpcio_testing/grpc_testing'
    'src/python/grpcio_status/grpc_status'
    'src/python/grpcio_observability/grpc_observability'
    'tools/run_tests/xds_k8s_test_driver/bin'
    'tools/run_tests/xds_k8s_test_driver/framework'
)

TEST_DIRS=(
    'src/python/grpcio_tests/tests'
    'src/python/grpcio_tests/tests_gevent'
    'tools/run_tests/xds_k8s_test_driver/tests'
)

VIRTUALENV=python_pylint_venv
python3 -m virtualenv $VIRTUALENV -p $(which python3)

PYTHON=$VIRTUALENV/bin/python

$PYTHON -m pip install --upgrade pip==19.3.1

# TODO(https://github.com/grpc/grpc/issues/23394): Update Pylint.
$PYTHON -m pip install --upgrade astroid==2.3.3 pylint==2.2.2 "isort>=4.3.0,<5.0.0"

EXIT=0
for dir in "${DIRS[@]}"; do
  $PYTHON -m pylint --rcfile=.pylintrc -rn "$dir" ${IGNORE_PATTERNS}  || EXIT=1
done

for dir in "${TEST_DIRS[@]}"; do
  $PYTHON -m pylint --rcfile=.pylintrc-tests -rn "$dir" ${IGNORE_PATTERNS} || EXIT=1
done

find examples/python \
  -iname "*.py" \
  -not -name "*_pb2.py" \
  -not -name "*_pb2_grpc.py" \
  | xargs $PYTHON -m pylint --rcfile=.pylintrc-examples -rn ${IGNORE_PATTERNS}

exit $EXIT
