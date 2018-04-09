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

set -ex

cd "$(dirname "$0")/../../.."

base=$(pwd)

mkdir -p artifacts/

# All the ruby packages have been built in the artifact phase already
# and we only collect them here to deliver them to the distribtest phase.
# Jenkins flow (deprecated)
cp -r "${EXTERNAL_GIT_ROOT}"/platform={windows,linux,macos}/artifacts/ruby_native_gem_*/* artifacts/ || true
# Kokoro flow
cp -r "${EXTERNAL_GIT_ROOT}"/input_artifacts/ruby_native_gem_*/* artifacts/ || true

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
    if [ "${KOKORO_JOB_NAME}" != "" ]
    then
      input_dir="${EXTERNAL_GIT_ROOT}/input_artifacts/protoc_${plat}_${arch}"
    else
      input_dir="${EXTERNAL_GIT_ROOT}/platform=${plat}/artifacts/protoc_${plat}_${arch}"
    fi
    output_dir="$base/src/ruby/tools/bin/${ruby_arch}-${plat}"
    mkdir -p "$output_dir"/google/protobuf
    mkdir -p "$output_dir"/google/protobuf/compiler  # needed for plugin.proto
    cp "$input_dir"/protoc* "$input_dir"/grpc_ruby_plugin* "$output_dir/"
    if [[ "$plat" != "windows" ]]
    then
      chmod +x "$output_dir/protoc" "$output_dir/grpc_ruby_plugin"
    fi
    for proto in "${well_known_protos[@]}"; do
      cp "$base/third_party/protobuf/src/google/protobuf/$proto.proto" "$output_dir/google/protobuf/$proto.proto"
    done
  done
done

cd "$base/src/ruby/tools"
gem build grpc-tools.gemspec
cp ./grpc-tools*.gem "$base/artifacts/"
