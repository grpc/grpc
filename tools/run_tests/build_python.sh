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

TOX_PYTHON_ENV="$1"

ROOT=`pwd`
export LD_LIBRARY_PATH=$ROOT/libs/$CONFIG
export DYLD_LIBRARY_PATH=$ROOT/libs/$CONFIG
export PATH=$ROOT/bins/$CONFIG:$ROOT/bins/$CONFIG/protobuf:$PATH
export CFLAGS="-I$ROOT/include -std=gnu99"
export LDFLAGS="-L$ROOT/libs/$CONFIG"
export GRPC_PYTHON_BUILD_WITH_CYTHON=1
export GRPC_PYTHON_USE_PRECOMPILED_BINARIES=0

if [ "$CONFIG" = "gcov" ]
then
  export GRPC_PYTHON_ENABLE_CYTHON_TRACING=1
fi

tox -e ${TOX_PYTHON_ENV} --notest

$ROOT/.tox/${TOX_PYTHON_ENV}/bin/python $ROOT/setup.py build
$ROOT/.tox/${TOX_PYTHON_ENV}/bin/python $ROOT/setup.py build_py
$ROOT/.tox/${TOX_PYTHON_ENV}/bin/python $ROOT/setup.py build_ext --inplace
$ROOT/.tox/${TOX_PYTHON_ENV}/bin/python $ROOT/setup.py gather --test

# Build the health checker
$ROOT/.tox/${TOX_PYTHON_ENV}/bin/python $ROOT/src/python/grpcio_health_checking/setup.py build
$ROOT/.tox/${TOX_PYTHON_ENV}/bin/python $ROOT/src/python/grpcio_health_checking/setup.py build_py
