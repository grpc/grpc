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
#
# This script is invoked by build_docker_* inside a docker
# container. You should never need to call this script on your own.

set -ex

if [ "$RELATIVE_COPY_PATH" == "" ]
then
  mkdir -p /var/local/git
  git clone $EXTERNAL_GIT_ROOT /var/local/git/grpc
  # clone gRPC submodules, use data from locally cloned submodules where possible
  (cd ${EXTERNAL_GIT_ROOT} && git submodule foreach 'cd /var/local/git/grpc \
  && git submodule update --init --reference ${EXTERNAL_GIT_ROOT}/${name} \
  ${name}')
else
  mkdir -p "/var/local/git/grpc/$RELATIVE_COPY_PATH"
  cp -r "$EXTERNAL_GIT_ROOT/$RELATIVE_COPY_PATH"/* "/var/local/git/grpc/$RELATIVE_COPY_PATH"
fi

$POST_GIT_STEP

if [ -x "$(command -v rvm)" ]
then
  rvm use ruby-2.1
fi

cd /var/local/git/grpc

$RUN_COMMAND
