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

source ~/.nvm/nvm.sh

nvm use 4
set -ex

cd $(dirname $0)/../..

base=$(pwd)

artifacts=$base/artifacts

mkdir -p $artifacts
cp -r $EXTERNAL_GIT_ROOT/architecture={x86,x64},language=node,platform={windows,linux,macos}/artifacts/* $artifacts/ || true

npm update
npm pack

cp grpc-*.tgz $artifacts/grpc.tgz

mkdir -p bin

cd src/node/tools
npm update
npm pack
cp grpc-tools-*.tgz $artifacts/
tools_version=$(npm list | grep -oP '(?<=grpc-tools@)\S+')

output_dir=$artifacts/grpc-precompiled-binaries/node/grpc-tools/v$tools_version
mkdir -p $output_dir

for arch in {x86,x64}; do
  case arch in
    x86)
      node_arch=ia32
      ;;
    *)
      node_arch=$arch
      ;;
  esac
  for plat in {windows,linux,macos}; do
    case plat in
      windows)
        node_plat=win32
        ;;
      macos)
        node_plat=darwin
        ;;
      *)
        node_plat=$plat
        ;;
    esac
    rm bin/*
    input_dir="$EXTERNAL_GIT_ROOT/architecture=$arch,language=protoc,platform=$plat/artifacts"
    cp $input_dir/protoc* bin/
    cp $input_dir/grpc_node_plugin* bin/
    tar -czf $output_dir/$node_plat-$node_arch.tar.gz bin/
  done
done
