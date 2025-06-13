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
    'src/python/grpcio_tests/tests'
    'src/python/grpcio_tests/tests_gevent'
    'examples/python'
)

VIRTUALENV=venv_ruff
python3.11 -m venv $VIRTUALENV
source $VIRTUALENV/bin/activate

python3 -m pip install --upgrade ruff

EXIT=0
ruff check --config ruff.toml "${DIRS[@]}" || EXIT=1

exit $EXIT 