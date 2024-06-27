#!/bin/bash
# Copyright 2021 The gRPC Authors
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

# avoid slow finalization after the script has exited.
source $(dirname $0)/../../../tools/internal_ci/helper_scripts/move_src_tree_and_respawn_itself_rc

# change to grpc repo root
cd $(dirname $0)/../../..

# source tools/internal_ci/helper_scripts/prepare_build_linux_rc

# some distribtests use a pre-registered binfmt_misc hook
# to automatically execute foreign binaries (such as aarch64)
# under qemu emulator.
# source tools/internal_ci/helper_scripts/prepare_qemu_rc

# configure ccache
source tools/internal_ci/helper_scripts/prepare_ccache_rc

# unit-tests setup starts from here

function maybe_run_command () {
  if python setup.py --help-commands | grep "$1" &>/dev/null; then
    python setup.py "$1";
  fi
}

BASEDIR=$(dirname "$0")

PACKAGES="grpcio_channelz  grpcio_csds  grpcio_admin grpcio_health_checking  grpcio_reflection  grpcio_status  grpcio_testing grpcio_csm_observability grpcio_tests"

echo $BASEDIR
echo $PACKAGES

cd "$BASEDIR";
pip install --upgrade "cython<3.0.0rc1";
python setup.py install;
pushd tools/distrib/python/grpcio_tools;
  ../make_grpcio_tools.py
  GRPC_PYTHON_BUILD_WITH_CYTHON=1 pip install .
popd;
pushd src/python/grpcio_observability;
  ./make_grpcio_observability.py
  GRPC_PYTHON_BUILD_WITH_CYTHON=1 pip install .
popd;
pushd tools/distrib/python/xds_protos;
  GRPC_PYTHON_BUILD_WITH_CYTHON=1 pip install .
popd;
pushd src/python;
  for PACKAGE in ${PACKAGES}; do
    pushd "${PACKAGE}";
      python setup.py clean;
      maybe_run_command preprocess
      maybe_run_command build_package_protos
      python -m pip install .;
    popd;
  done
popd;
pushd src/python/grpcio_tests;
  python setup.py test_lite
#  python setup.py test_aio
  python setup.py test_py3_only
popd;

