#!/bin/bash
# Copyright 2024 The gRPC Authors
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

BASEDIR=$(dirname "$0")/../..
cd "$BASEDIR";

# Run tests for grpcio_tests
pushd src/python/grpcio_tests;
  python3 setup.py test_lite
  python3 setup.py test_aio
  python3 setup.py test_py3_only
popd;

chmod -R 755 src/
