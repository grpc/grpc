#!/bin/bash
# Copyright 2016 gRPC authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

source ~/.nvm/nvm.sh

nvm use 8
set -ex

cd $(dirname $0)/../../..

base=$(pwd)

artifacts=$base/artifacts

mkdir -p $artifacts
cp -r $EXTERNAL_GIT_ROOT/platform={windows,linux,macos}/artifacts/node_ext_*/* $artifacts/ || true

npm update
npm pack

cp grpc-*.tgz $artifacts/grpc.tgz

mkdir -p bin

cd $base/src/node/health_check
npm pack
cp grpc-health-check-*.tgz $artifacts/

cd $base/src/node/tools
npm update
npm pack
cp grpc-tools-*.tgz $artifacts/
tools_version=$(npm list | grep -oP '(?<=grpc-tools@)\S+')

output_dir=$artifacts/grpc-precompiled-binaries/node/grpc-tools/v$tools_version
mkdir -p $output_dir

well_known_protos=( any api compiler/plugin descriptor duration empty field_mask source_context struct timestamp type wrappers )

for arch in {x86,x64}; do
  case $arch in
    x86)
      node_arch=ia32
      ;;
    *)
      node_arch=$arch
      ;;
  esac
  for plat in {windows,linux,macos}; do
    case $plat in
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
    rm -r bin/*
    input_dir="$EXTERNAL_GIT_ROOT/platform=${plat}/artifacts/protoc_${plat}_${arch}"
    cp $input_dir/protoc* bin/
    cp $input_dir/grpc_node_plugin* bin/
    mkdir -p bin/google/protobuf
    mkdir -p bin/google/protobuf/compiler  # needed for plugin.proto
    for proto in "${well_known_protos[@]}"; do
      cp $base/third_party/protobuf/src/google/protobuf/$proto.proto bin/google/protobuf/$proto.proto
    done
    tar -czf $output_dir/$node_plat-$node_arch.tar.gz bin/
  done
done
