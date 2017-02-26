#!/bin/bash
# Copyright 2015, gRPC authors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

set -ex

# change to root directory
cd $(dirname $0)/../..

DIRS=src/python
EXCLUSIONS='src/python/grpcio/grpc_*.py src/python/grpcio_health_checking/grpc_*.py src/python/grpcio_reflection/grpc_*.py src/python/grpcio_tests/grpc_*.py'

VIRTUALENV=python_format_venv

virtualenv $VIRTUALENV
PYTHON=`realpath $VIRTUALENV/bin/python`
$PYTHON -m pip install futures
$PYTHON -m pip install yapf==0.16.0

exclusion_args=""
for exclusion in $EXCLUSIONS; do
  exclusion_args="$exclusion_args --exclude $exclusion"
done

script_result=0
for dir in $DIRS; do
  tempdir=`mktemp -d`
  cp -RT $dir $tempdir
  $PYTHON -m yapf -i -r -p $exclusion_args $dir
  if ! diff -r $dir $tempdir; then
    script_result=1
  fi
  rm -rf $tempdir
done
exit $script_result
