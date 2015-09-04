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

ROOT=`pwd`
GRPCIO_TEST=$ROOT/src/python/grpcio_test
export LD_LIBRARY_PATH=$ROOT/libs/$CONFIG
export DYLD_LIBRARY_PATH=$ROOT/libs/$CONFIG
export PATH=$ROOT/bins/$CONFIG:$ROOT/bins/$CONFIG/protobuf:$PATH
source "python"$PYVER"_virtual_environment"/bin/activate

# TODO(atash): These tests don't currently run under py.test and thus don't
# appear under the coverage report. Find a way to get these tests to work with
# py.test (or find another tool or *something*) that's acceptable to the rest of
# the team...
"python"$PYVER -m grpc_test._core_over_links_base_interface_test
"python"$PYVER -m grpc_test._crust_over_core_over_links_face_interface_test
"python"$PYVER -m grpc_test.beta._face_interface_test
"python"$PYVER -m grpc_test.framework._crust_over_core_face_interface_test
"python"$PYVER -m grpc_test.framework.core._base_interface_test

"python"$PYVER $GRPCIO_TEST/setup.py test -a "-n8 --cov=grpc --junitxml=./report.xml --timeout=300"
