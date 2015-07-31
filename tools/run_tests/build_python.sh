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

root=`pwd`

make_virtualenv() {
  virtualenv_name="python"$1"_virtual_environment"
  if [ ! -d $virtualenv_name ]
  then
    # Build the entire virtual environment
    virtualenv -p `which "python"$1` $virtualenv_name
    source $virtualenv_name/bin/activate
    pip install -r src/python/requirements.txt
    CFLAGS="-I$root/include -std=c89" LDFLAGS=-L$root/libs/$CONFIG GRPC_PYTHON_BUILD_WITH_CYTHON=1 pip install src/python/grpcio
    pip install src/python/grpcio_test
  else
    source $virtualenv_name/bin/activate
    # Uninstall and re-install the packages we care about. Don't use
    # --force-reinstall or --ignore-installed to avoid propagating this
    # unnecessarily to dependencies. Don't use --no-deps to avoid missing
    # dependency upgrades.
    (yes | pip uninstall grpcio) || true
    (yes | pip uninstall grpcio_test) || true
    (CFLAGS="-I$root/include -std=c89" LDFLAGS=-L$root/libs/$CONFIG GRPC_PYTHON_BUILD_WITH_CYTHON=1 pip install src/python/grpcio) || (
      # Fall back to rebuilding the entire environment
      rm -rf $virtualenv_name
      make_virtualenv $1
    )
    pip install src/python/grpcio_test
  fi
}

make_virtualenv $1
