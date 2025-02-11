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
    'src/python/grpcio_csm_observability/grpc_csm_observability'
)

TEST_DIRS=(
    'src/python/grpcio_tests/tests'
    'src/python/grpcio_tests/tests_gevent'
)

VIRTUALENV=venv_python_code
python3.7 -m virtualenv $VIRTUALENV
source $VIRTUALENV/bin/activate

# TODO(https://github.com/grpc/grpc/issues/23394): Update Pylint.
python3 -m pip install --upgrade astroid==2.3.3 \
  pylint==2.2.2 \
  toml==0.10.2 \
  "isort>=4.3.0,<5.0.0"

EXIT=0
for dir in "${DIRS[@]}"; do
  python3 -m pylint --rcfile=.pylintrc -rn "$dir" ${IGNORE_PATTERNS}  || EXIT=1
done

for dir in "${TEST_DIRS[@]}"; do
  python3 -m pylint --rcfile=.pylintrc-tests -rn "$dir" ${IGNORE_PATTERNS} || EXIT=1
done

find examples/python \
  -iname "*.py" \
  -not -name "*_pb2.py" \
  -not -name "*_pb2_grpc.py" \
  | xargs python3 -m pylint --rcfile=.pylintrc-examples -rn ${IGNORE_PATTERNS}

exit $EXIT
