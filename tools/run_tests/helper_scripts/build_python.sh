#!/bin/bash
# Copyright 2015, Google Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

set -ex

# change to grpc repo root
cd $(dirname $0)/../../..

##########################
# Portability operations #
##########################

PLATFORM=`uname -s`

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

# Associated virtual environment name for the given python command.
function venv() {
  $1 -c "import sys; print('py{}{}'.format(*sys.version_info[:2]))"
}

# Path to python executable within a virtual environment depending on the
# system.
function venv_relative_python() {
  if [ $(is_mingw) ]; then
    echo 'Scripts/python.exe'
  else
    echo 'bin/python'
  fi
}

# Distutils toolchain to use depending on the system.
function toolchain() {
  if [ $(is_mingw) ]; then
    echo 'mingw32'
  else
    echo 'unix'
  fi
}

# Command to invoke the linux command `realpath` or equivalent.
function script_realpath() {
  # Find `realpath`
  if [ -x "$(command -v realpath)" ]; then
    realpath "$@"
  elif [ -x "$(command -v grealpath)" ]; then
    grealpath "$@"
  else
    exit 1
  fi
}

####################
# Script Arguments #
####################

PYTHON=${1:-python2.7}
VENV=${2:-$(venv $PYTHON)}
VENV_RELATIVE_PYTHON=${3:-$(venv_relative_python)}
TOOLCHAIN=${4:-$(toolchain)}

if [ $(is_msys) ]; then
  echo "MSYS doesn't directly provide the right compiler(s);"
  echo "switch to a MinGW shell."
  exit 1
fi

ROOT=`pwd`
export CFLAGS="-I$ROOT/include -std=gnu99 -fno-wrapv $CFLAGS"
export GRPC_PYTHON_BUILD_WITH_CYTHON=1
export LANG=en_US.UTF-8

# Default python on the host to fall back to when instantiating e.g. the
# virtualenv.
HOST_PYTHON=${HOST_PYTHON:-python}

# If ccache is available on Linux, use it.
if [ $(is_linux) ]; then
  # We're not on Darwin (Mac OS X)
  if [ -x "$(command -v ccache)" ]; then
    if [ -x "$(command -v gcc)" ]; then
      export CC='ccache gcc'
    elif [ -x "$(command -v clang)" ]; then
      export CC='ccache clang'
    fi
  fi
fi

############################
# Perform build operations #
############################

# Instnatiate the virtualenv, preferring to do so from the relevant python
# version. Even if these commands fail (e.g. on Windows due to name conflicts)
# it's possible that the virtualenv is still usable and we trust the tester to
# be able to 'figure it out' instead of us e.g. doing potentially expensive and
# unnecessary error recovery by `rm -rf`ing the virtualenv.
($PYTHON -m virtualenv $VENV ||
 $HOST_PYTHON -m virtualenv -p $PYTHON $VENV ||
 true)
VENV_PYTHON=`script_realpath "$VENV/$VENV_RELATIVE_PYTHON"`

# pip-installs the directory specified. Used because on MSYS the vanilla Windows
# Python gets confused when parsing paths.
pip_install_dir() {
  PWD=`pwd`
  cd $1
  ($VENV_PYTHON setup.py build_ext -c $TOOLCHAIN || true)
  $VENV_PYTHON -m pip install --no-deps .
  cd $PWD
}

$VENV_PYTHON -m pip install --upgrade pip
$VENV_PYTHON -m pip install setuptools
$VENV_PYTHON -m pip install cython
$VENV_PYTHON -m pip install six enum34 protobuf futures
pip_install_dir $ROOT

$VENV_PYTHON $ROOT/tools/distrib/python/make_grpcio_tools.py
pip_install_dir $ROOT/tools/distrib/python/grpcio_tools

# Build/install health checking
$VENV_PYTHON $ROOT/src/python/grpcio_health_checking/setup.py preprocess
$VENV_PYTHON $ROOT/src/python/grpcio_health_checking/setup.py build_package_protos
pip_install_dir $ROOT/src/python/grpcio_health_checking

# Build/install reflection
$VENV_PYTHON $ROOT/src/python/grpcio_reflection/setup.py preprocess
$VENV_PYTHON $ROOT/src/python/grpcio_reflection/setup.py build_package_protos
pip_install_dir $ROOT/src/python/grpcio_reflection

# Build/install tests
$VENV_PYTHON -m pip install coverage oauth2client
$VENV_PYTHON $ROOT/src/python/grpcio_tests/setup.py preprocess
$VENV_PYTHON $ROOT/src/python/grpcio_tests/setup.py build_package_protos
pip_install_dir $ROOT/src/python/grpcio_tests
