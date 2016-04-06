#!/bin/bash
# Copyright 2016, Google Inc.
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

cd $(dirname $0)/../..

if [ "$SKIP_PIP_INSTALL" == "" ]
then
  pip install --upgrade six
  # There's a bug in newer versions of setuptools (see
  # https://bitbucket.org/pypa/setuptools/issues/503/pkg_resources_vendorpackagingrequirementsi)
  pip install --upgrade 'setuptools==18'
  pip install -rrequirements.txt
fi

export GRPC_PYTHON_USE_CUSTOM_BDIST=0
export GRPC_PYTHON_BUILD_WITH_CYTHON=1

# Build the source distribution first because MANIFEST.in cannot override
# exclusion of built shared objects among package resources (for some
# inexplicable reason).
${SETARCH_CMD} python setup.py  \
    sdist

# The bdist_wheel_grpc_custom command is finicky about command output ordering
# and thus ought to be run in a shell command separate of others. Further, it
# trashes the actual bdist_wheel output, so it should be run first so that
# bdist_wheel may be run unmolested.
${SETARCH_CMD} python setup.py  \
    build_tagged_ext

# Wheel has a bug where directories don't get excluded.
# https://bitbucket.org/pypa/wheel/issues/99/cannot-exclude-directory
${SETARCH_CMD} python setup.py  \
    bdist_wheel

mkdir -p artifacts

cp -r dist/* artifacts
