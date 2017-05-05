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

NODE_TARGET_ARCH=$1
source ~/.nvm/nvm.sh

nvm use 4
set -ex

cd $(dirname $0)/../../..

rm -rf build || true

mkdir -p artifacts

npm update

node_versions=( 4.0.0 5.0.0 6.0.0 7.0.0 )

electron_versions=( 1.0.0 1.1.0 1.2.0 1.3.0 1.4.0 1.5.0 1.6.0 )

for version in ${node_versions[@]}
do
  ./node_modules/.bin/node-pre-gyp configure rebuild package testpackage --target=$version --target_arch=$NODE_TARGET_ARCH --grpc_alpine=true
  cp -r build/stage/* artifacts/
done

for version in ${electron_versions[@]}
do
  HOME=~/.electron-gyp ./node_modules/.bin/node-pre-gyp configure rebuild package testpackage --runtime=electron --target=$version --target_arch=$NODE_TARGET_ARCH --disturl=https://atom.io/download/electron
  cp -r build/stage/* artifacts/
done
