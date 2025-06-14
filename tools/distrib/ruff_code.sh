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
    'src/python/grpcio/grpc/_grpcio_metadata.py'
    'src/python/grpcio/grpc/_decorator.py'
    'src/python/grpcio/grpc/_auth.py'
    'src/python/grpcio/grpc/_channel.py'
)

VIRTUALENV=venv_ruff
python3.11 -m virtualenv $VIRTUALENV
source $VIRTUALENV/bin/activate

python3 -m pip install --upgrade ruff==0.11.13

EXIT=0
ruff check --config ruff.toml "${DIRS[@]}" || EXIT=1

exit $EXIT 