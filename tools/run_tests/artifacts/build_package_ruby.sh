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

cd $(dirname $0)/../../..

base=$(pwd)

mkdir -p artifacts/

# All the ruby packages have been built in the artifact phase already
# and we only collect them here to deliver them to the distribtest phase.
cp -r $EXTERNAL_GIT_ROOT/architecture={x86,x64},language=ruby,platform={windows,linux,macos}/artifacts/* artifacts/ || true

well_known_protos=( any api compiler/plugin descriptor duration empty field_mask source_context struct timestamp type wrappers )

# TODO: all the artifact builder configurations generate a grpc-VERSION.gem
# source distribution package, and only one of them will end up
# in the artifacts/ directory. They should be all equivalent though.

for arch in {x86,x64}; do
  case $arch in
    x64)
      ruby_arch=x86_64
      ;;
    *)
      ruby_arch=$arch
      ;;
  esac
  for plat in {windows,linux,macos}; do
    input_dir="$EXTERNAL_GIT_ROOT/architecture=$arch,language=protoc,platform=$plat/artifacts"
    output_dir="$base/src/ruby/tools/bin/${ruby_arch}-${plat}"
    mkdir -p $output_dir/google/protobuf
    mkdir -p $output_dir/google/protobuf/compiler  # needed for plugin.proto
    cp $input_dir/protoc* $output_dir/
    cp $input_dir/grpc_ruby_plugin* $output_dir/
    for proto in "${well_known_protos[@]}"; do
      cp $base/third_party/protobuf/src/google/protobuf/$proto.proto $output_dir/google/protobuf/$proto.proto
    done
  done
done

cd $base/src/ruby/tools
gem build grpc-tools.gemspec
cp ./grpc-tools*.gem $base/artifacts/
