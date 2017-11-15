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

# change to grpc repo root
cd $(dirname $0)/../../..

PYTHON=`realpath "${1:-py27/bin/python}"`

ROOT=`pwd`

# compiles proto files to pb2.py and pb2_grpc.py
# usage: proto_to_pb2 proto_file package_root target_dir_relative_to_package
proto_to_pb2() {
  PWD="$(pwd)"
  cd "$2"
  $PYTHON -m grpc_tools.protoc "$1" "-I$(dirname "$1")" \
    "--python_out=$2/$3" "--grpc_python_out=$2/$3"
  cd "$PWD"
}
# Build health-checking and reflection protos before running the tests
proto_to_pb2 "$ROOT/src/proto/grpc/health/v1/health.proto" \
  "$ROOT/src/python/grpcio_health_checking" "grpc_health/v1"
proto_to_pb2 "$ROOT/src/proto/grpc/reflection/v1alpha/reflection.proto" \
  "$ROOT/src/python/grpcio_reflection" "grpc_reflection/v1alpha"

# Install testing
$PYTHON $ROOT/src/python/grpcio_tests/setup.py test_lite

mkdir -p $ROOT/reports
rm -rf $ROOT/reports/python-coverage
(mv -T $ROOT/htmlcov $ROOT/reports/python-coverage) || true

