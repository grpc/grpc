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

cd $(dirname $0)

# Pick up the source dist archive whatever its version is
BDIST_ARCHIVES=$EXTERNAL_GIT_ROOT/input_artifacts/grpcio-*.whl
TOOLS_BDIST_ARCHIVES=$EXTERNAL_GIT_ROOT/input_artifacts/grpcio_tools-*.whl

if [ ! -f ${SDIST_ARCHIVE} ]
then
  echo "Archive ${SDIST_ARCHIVE} does not exist."
  exit 1
fi

PYTHON=python2
PIP=pip2
which $PYTHON || PYTHON=python
which $PIP || PIP=pip

# TODO(jtattermusch): this shouldn't be required
# TODO(jtattermusch): run the command twice to workaround docker-on-overlay
# issue https://github.com/docker/docker/issues/12327
# (first attempt will fail when using docker with overlayFS)
${PIP} install --upgrade six pip || ${PIP} install --upgrade six pip

# At least one of the bdist packages has to succeed (whichever one matches the
# test machine, anyway).
for bdist in ${BDIST_ARCHIVES} ${TOOLS_BDIST_ARCHIVES}; do
  ($PYTHON -m pip install $bdist) || true
done

# TODO(jtattermusch): add a .proto file to the distribtest, generate python
# code from it and then use the generated code from distribtest.py
$PYTHON -m grpc.tools.protoc

$PYTHON distribtest.py
