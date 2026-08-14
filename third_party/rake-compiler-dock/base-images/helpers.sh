#!/bin/bash

# Copyright 2026 gRPC authors.
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

set -euo pipefail

if [[ -z "${GEM_ROOT:-}" || -z "${GRPC_ROOT:-}" ]]; then
  echo "Please set environment variables GEM_ROOT and GRPC_ROOT" >&2
  exit 1
fi

ARCHS=(
  aarch64-linux-gnu
  aarch64-linux-musl
  arm64-darwin
  x64-mingw-ucrt
  x86_64-darwin
  x86_64-linux-gnu
  x86_64-linux-musl
  x86-linux-gnu
  x86-linux-musl
  x86-mingw32
)

IMAGE_DIR="${GRPC_ROOT}/third_party/rake-compiler-dock"
BASE_IMAGE_DIR="${IMAGE_DIR}/base-images"

clone_rake_compiler_dock_repo() {
  # We clone the repo using umask 0022 because the build process will execute
  # scripts under `build/` using a different linux user inside docker,
  # therefore we need to grant necessary read/write permission to other users.
  (umask 0022; git clone https://github.com/rake-compiler/rake-compiler-dock -b v1.12.0 "${GEM_ROOT}")
}

generate_rake_compiler_dockerfiles() {
  cd "${GEM_ROOT}" || exit 1
  bundle config set --local path '.bundle/gems'
  bundle install
  bundle exec ruby -e "load 'Rakefile'"
}

apply_patch() {
  cd "${GEM_ROOT}" || exit 1
  git apply "${GRPC_ROOT}/third_party/rake-compiler-dock/base-images/update_cross_compilers.patch"
}

copy_dockerfiles() {
  local docker_dir="${GEM_ROOT}/tmp/docker"
  for arch in "${ARCHS[@]}"; do
    local src="${docker_dir}/Dockerfile.mri.${arch}"
    local dst="${BASE_IMAGE_DIR}/rake_${arch}"
    mkdir -p "$dst"
    cp "$src" "$dst/Dockerfile"
  done
}

copy_build_folder() {
  rm -rf "${BASE_IMAGE_DIR}/build"
  cp -a "${GEM_ROOT}/build" "${BASE_IMAGE_DIR}"
}

rewrite_base_images_references() {
  for arch in "${ARCHS[@]}"; do
    local current_version
    current_version=$(cat "${BASE_IMAGE_DIR}/rake_${arch}.current_version")
    sed -E -i "s|^FROM [^ ]+\$|FROM ${current_version}|" "${IMAGE_DIR}/rake_${arch}/Dockerfile"
  done
}

install_docker_files() {
  clone_rake_compiler_dock_repo
  apply_patch
  generate_rake_compiler_dockerfiles
  copy_dockerfiles
  copy_build_folder
}

if [ -z "${1:-}" ]; then
  exit 1
else
  "$@"
fi
