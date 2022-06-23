#!/bin/bash
# Copyright 2020 The gRPC Authors
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

echo "It's recommended that you run this script from a virtual environment."

set -ex

BASEDIR=$(dirname "$0")
BASEDIR=$(realpath "$BASEDIR")/../..

PACKAGES="grpcio_reflection grpcio_status grpcio_health_checking grpcio_channelz grpcio_csds grpcio_admin grpcio_testing grpcio_tests "

function run_command_if_available() {
  if python setup.py --help-commands | grep "$1"; then
    python setup.py "$1";
  fi
}

(cd "$BASEDIR";
  pip install --upgrade cython;
  python setup.py install;
  pushd tools/distrib/python/grpcio_tools;
    ../make_grpcio_tools.py
    GRPC_PYTHON_BUILD_WITH_CYTHON=1 pip install .
  popd;
  pushd src/python;
    for PACKAGE in ${PACKAGES}; do
      pushd "${PACKAGE}";
        run_command_if_available build_package_protos
        run_command_if_available preprocess
        python setup.py install;
      popd;
    done
  popd;
)
