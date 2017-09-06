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

cd $(dirname $0)

# Pick up the source dist archive whatever its version is
SDIST_ARCHIVES=$EXTERNAL_GIT_ROOT/input_artifacts/grpcio-*.tar.gz
BDIST_ARCHIVES=$EXTERNAL_GIT_ROOT/input_artifacts/grpcio-*.whl
TOOLS_SDIST_ARCHIVES=$EXTERNAL_GIT_ROOT/input_artifacts/grpcio_tools-*.tar.gz
TOOLS_BDIST_ARCHIVES=$EXTERNAL_GIT_ROOT/input_artifacts/grpcio_tools-*.whl

function make_virtualenv() {
  virtualenv $1
  $1/bin/python -m pip install --upgrade six pip
  $1/bin/python -m pip install cython
}

function at_least_one_installs() {
  for file in "$@"; do
    if python -m pip install $file; then
      return 0
    fi
  done
  return -1
}

make_virtualenv bdist_test
make_virtualenv sdist_test

#
# Install our distributions in order of dependencies
#

(source bdist_test/bin/activate && at_least_one_installs ${BDIST_ARCHIVES})
(source bdist_test/bin/activate && at_least_one_installs ${TOOLS_BDIST_ARCHIVES})

(source sdist_test/bin/activate && at_least_one_installs ${SDIST_ARCHIVES})
(source sdist_test/bin/activate && at_least_one_installs ${TOOLS_SDIST_ARCHIVES})

#
# Test our distributions
#

# TODO(jtattermusch): add a .proto file to the distribtest, generate python
# code from it and then use the generated code from distribtest.py
(source bdist_test/bin/activate && python -m grpc.tools.protoc --help)
(source sdist_test/bin/activate && python -m grpc.tools.protoc --help)

(source bdist_test/bin/activate && python distribtest.py)
(source sdist_test/bin/activate && python distribtest.py)
