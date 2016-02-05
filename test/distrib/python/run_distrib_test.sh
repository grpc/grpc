#!/bin/bash
# Copyright 2015-2016, Google Inc.
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

# TODO(jtattermusch): replace the version number
SDIST_ARCHIVE="$EXTERNAL_GIT_ROOT/input_artifacts/grpcio-0.12.0b8.tar.gz"
BDIST_DIR="file://$EXTERNAL_GIT_ROOT/input_artifacts"

if [ ! -f "${SDIST_ARCHIVE}" ]
then
  echo "Archive ${SDIST_ARCHIVE} does not exist."
  exit 1
fi

# TODO(jtattermusch): this shouldn't be required
pip install --upgrade six

# TODO(jtattermusch): if these don't get preinstalled, pip tries to install them
# with --use-grpc-custom-bdist option, which obviously fails.
pip install --upgrade enum34
pip install --upgrade futures

GRPC_PYTHON_BINARIES_REPOSITORY="${BDIST_DIR}" \
    pip install \
    "${SDIST_ARCHIVE}" \
    --install-option="--use-grpc-custom-bdist"

python distribtest.py
