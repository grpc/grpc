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
cd "$(dirname "$0")/../../.."

##########################
# Portability operations #
##########################

PLATFORM=$(uname -s)

function is_msys() {
  if [ "${PLATFORM/MSYS}" != "$PLATFORM" ]; then
    echo true
  else
    exit 1
  fi
}

function is_mingw() {
  if [ "${PLATFORM/MINGW}" != "$PLATFORM" ]; then
    echo true
  else
    exit 1
  fi
}

function is_darwin() {
  if [ "${PLATFORM/Darwin}" != "$PLATFORM" ]; then
    echo true
  else
    exit 1
  fi
}

function is_linux() {
  if [ "${PLATFORM/Linux}" != "$PLATFORM" ]; then
    echo true
  else
    exit 1
  fi
}

function inside_venv() {
  if [[ -n "${VIRTUAL_ENV}" ]]; then
    echo true
  fi
}

# Associated virtual environment name for the given python command.
function venv() {
  $1 -c "import sys; print('py{}{}'.format(*sys.version_info[:2]))"
}

# Path to python executable within a virtual environment depending on the
# system.
function venv_relative_python() {
  if [ "$(is_mingw)" ]; then
    echo 'Scripts/python.exe'
  else
    echo 'bin/python'
  fi
}

# Distutils toolchain to use depending on the system.
function toolchain() {
  if [ "$(is_mingw)" ]; then
    echo 'mingw32'
  else
    echo 'unix'
  fi
}

####################
# Script Arguments #
####################

PYTHON=${1:-python2.7}
VENV=${2:-$(venv "$PYTHON")}
VENV_RELATIVE_PYTHON=${3:-$(venv_relative_python)}
TOOLCHAIN=${4:-$(toolchain)}

if [ "$(is_msys)" ]; then
  echo "MSYS doesn't directly provide the right compiler(s);"
  echo "switch to a MinGW shell."
  exit 1
fi

ROOT=$(pwd)
export CFLAGS="-I$ROOT/include -fno-wrapv $CFLAGS"
export GRPC_PYTHON_BUILD_WITH_CYTHON=1
export LANG=en_US.UTF-8

# Allow build_ext to build C/C++ files in parallel
# by enabling a monkeypatch. It speeds up the build a lot.
DEFAULT_PARALLEL_JOBS=$(nproc) || DEFAULT_PARALLEL_JOBS=4
export GRPC_PYTHON_BUILD_EXT_COMPILER_JOBS=${GRPC_PYTHON_BUILD_EXT_COMPILER_JOBS:-$DEFAULT_PARALLEL_JOBS}

# activate ccache if desired
# shellcheck disable=SC1091
source tools/internal_ci/helper_scripts/prepare_ccache_symlinks_rc

############################
# Perform build operations #
############################

if [[ "$(inside_venv)" ]]; then
  VENV_PYTHON="$PYTHON"
else
  # Instantiate the virtualenv from the Python version passed in.
  $PYTHON -m pip install --user virtualenv==20.0.23
  $PYTHON -m virtualenv "$VENV"
  VENV_PYTHON="$(pwd)/$VENV/$VENV_RELATIVE_PYTHON"
fi


# On library/version/platforms combo that do not have a binary
# published, we may end up building a dependency from source. In that
# case, several of our build environment variables may disrupt the
# third-party build process. This function pipes through only the
# minimal environment necessary.
pip_install() {
  /usr/bin/env -i PATH="$PATH" "$VENV_PYTHON" -m pip install "$@"
}

# Pin setuptools to < 60.0.0 to restore the distutil installation, see:
# https://github.com/pypa/setuptools/pull/2896
export SETUPTOOLS_USE_DISTUTILS=stdlib
pip_install --upgrade pip==21.3.1
pip_install --upgrade setuptools==59.6.0

# pip-installs the directory specified. Used because on MSYS the vanilla Windows
# Python gets confused when parsing paths.
pip_install_dir() {
  PWD=$(pwd)
  cd "$1"
  ($VENV_PYTHON setup.py build_ext -c "$TOOLCHAIN" || true)
  $VENV_PYTHON -m pip install --no-deps .
  cd "$PWD"
}

pip_install_dir_and_deps() {
  PWD=$(pwd)
  cd "$1"
  ($VENV_PYTHON setup.py build_ext -c "$TOOLCHAIN" || true)
  $VENV_PYTHON -m pip install .
  cd "$PWD"
}

pip_install -U gevent

pip_install --upgrade 'cython<3.0.0rc1'
pip_install --upgrade six 'protobuf>=4.21.3rc1,!=4.22.0.*'

if [ "$("$VENV_PYTHON" -c "import sys; print(sys.version_info[0])")" == "2" ]
then
  pip_install --upgrade futures enum34
fi

pip_install_dir "$ROOT"

$VENV_PYTHON "$ROOT/tools/distrib/python/make_grpcio_tools.py"
pip_install_dir_and_deps "$ROOT/tools/distrib/python/grpcio_tools"

# Build/install Observability
$VENV_PYTHON "$ROOT/src/python/grpcio_observability/make_grpcio_observability.py"
pip_install_dir_and_deps "$ROOT/src/python/grpcio_observability"

# Build/install Channelz
$VENV_PYTHON "$ROOT/src/python/grpcio_channelz/setup.py" preprocess
$VENV_PYTHON "$ROOT/src/python/grpcio_channelz/setup.py" build_package_protos
pip_install_dir "$ROOT/src/python/grpcio_channelz"

# Build/install health checking
$VENV_PYTHON "$ROOT/src/python/grpcio_health_checking/setup.py" preprocess
$VENV_PYTHON "$ROOT/src/python/grpcio_health_checking/setup.py" build_package_protos
pip_install_dir "$ROOT/src/python/grpcio_health_checking"

# Build/install reflection
$VENV_PYTHON "$ROOT/src/python/grpcio_reflection/setup.py" preprocess
$VENV_PYTHON "$ROOT/src/python/grpcio_reflection/setup.py" build_package_protos
pip_install_dir "$ROOT/src/python/grpcio_reflection"

# Build/install status proto mapping
$VENV_PYTHON "$ROOT/src/python/grpcio_status/setup.py" preprocess
$VENV_PYTHON "$ROOT/src/python/grpcio_status/setup.py" build_package_protos
pip_install_dir "$ROOT/src/python/grpcio_status"


# Build/install status proto mapping
$VENV_PYTHON "$ROOT/tools/distrib/python/xds_protos/build.py"
pip_install_dir "$ROOT/tools/distrib/python/xds_protos"

# Build/install csds
pip_install_dir "$ROOT/src/python/grpcio_csds"

# Build/install admin
pip_install_dir "$ROOT/src/python/grpcio_admin"

# Install testing
pip_install_dir "$ROOT/src/python/grpcio_testing"

# Build/install tests
pip_install coverage==4.4 oauth2client==4.1.0 \
            google-auth>=1.35.0 requests==2.31.0 \
            googleapis-common-protos>=1.5.5 rsa==4.0 absl-py==1.4.0
$VENV_PYTHON "$ROOT/src/python/grpcio_tests/setup.py" preprocess
$VENV_PYTHON "$ROOT/src/python/grpcio_tests/setup.py" build_package_protos
pip_install_dir "$ROOT/src/python/grpcio_tests"
