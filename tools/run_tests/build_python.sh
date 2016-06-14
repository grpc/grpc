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
cd $(dirname $0)/../..

# Arguments
PYTHON=${1:-python2.7}
VENV=${2:-py27}
VENV_RELATIVE_PYTHON=${3:-bin/python}
TOOLCHAIN=${4:-unix}

ROOT=`pwd`
export CFLAGS="-I$ROOT/include -std=gnu99 -fno-wrapv"
export GRPC_PYTHON_BUILD_WITH_CYTHON=1

# Default python on the host to fall back to when instantiating e.g. the
# virtualenv.
HOST_PYTHON=${HOST_PYTHON:-python}

# If ccache is available, use it... unless we're on Mac, then all hell breaks
# loose because Python does hacky things to support other hacky things done to
# hacky things on Mac OS X
PLATFORM=`uname -s`
if [ "${PLATFORM/Darwin}" = "$PLATFORM" ]; then
  # We're not on Darwin (Mac OS X)
  if [ -x "$(command -v ccache)" ]; then
    if [ -x "$(command -v gcc)" ]; then
      export CC='ccache gcc'
    elif [ -x "$(command -v clang)" ]; then
      export CC='ccache clang'
    fi
  fi
fi

# Find `realpath`
if [ -x "$(command -v realpath)" ]; then
  export REALPATH=realpath
elif [ -x "$(command -v grealpath)" ]; then
  export REALPATH=grealpath
else
  echo 'Couldn'"'"'t find `realpath` or `grealpath`'
  exit 1
fi

if [ "$CONFIG" = "gcov" ]; then
  export GRPC_PYTHON_ENABLE_CYTHON_TRACING=1
fi

# Instnatiate the virtualenv, preferring to do so from the relevant python
# version. Even if these commands fail (e.g. on Windows due to name conflicts)
# it's possible that the virtualenv is still usable and we trust the tester to
# be able to 'figure it out' instead of us e.g. doing potentially expensive and
# unnecessary error recovery by `rm -rf`ing the virtualenv.
($PYTHON -m virtualenv $VENV ||
 $HOST_PYTHON -m virtualenv -p $PYTHON $VENV ||
 true)
VENV_PYTHON=`$REALPATH -s "$VENV/$VENV_RELATIVE_PYTHON"`

# pip-installs the directory specified. Used because on MSYS the vanilla Windows
# Python gets confused when parsing paths.
pip_install_dir() {
  PWD=`pwd`
  cd $1
  ($VENV_PYTHON setup.py build_ext -c $TOOLCHAIN || true)
  # install the dependencies
  $VENV_PYTHON -m pip install --upgrade .
  # ensure that we've reinstalled the test packages
  $VENV_PYTHON -m pip install --upgrade --force-reinstall --no-deps .
  cd $PWD
}

$VENV_PYTHON -m pip install --upgrade pip setuptools
$VENV_PYTHON -m pip install cython
pip_install_dir $ROOT
$VENV_PYTHON $ROOT/tools/distrib/python/make_grpcio_tools.py
pip_install_dir $ROOT/tools/distrib/python/grpcio_tools
# TODO(atash) figure out namespace packages and grpcio-tools and auditwheel
# etc...
pip_install_dir $ROOT
$VENV_PYTHON $ROOT/src/python/grpcio_health_checking/setup.py preprocess
pip_install_dir $ROOT/src/python/grpcio_health_checking
$VENV_PYTHON $ROOT/src/python/grpcio_tests/setup.py preprocess
$VENV_PYTHON $ROOT/src/python/grpcio_tests/setup.py build_proto_modules
pip_install_dir $ROOT/src/python/grpcio_tests
