#!/bin/bash
# Copyright 2022 The gRPC Authors
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

# Source this rc script to create symlinks to ccache

if [ "${GRPC_BUILD_ENABLE_CCACHE}" != "" ] && [ "${GRPC_BUILD_ENABLE_CCACHE}" != "false" ] && [ "${GRPC_BUILD_ENABLE_CCACHE}" != "0" ]
then
  if [ -x "$(command -v ccache)" ]
  then
    SUPPORTED_COMPILERS=(
      # common compiler binaries
      "gcc"
      "g++"
      "clang"
      "clang++"
      "cc"
      "c++"
      # ruby artifacts: rake-compiler-dock crosscompilers
      # TODO(jtattermusch): ensure that the list of ruby crosscompilers stays up to date.
      "x86_64-redhat-linux-gcc"
      "x86_64-redhat-linux-g++"
      "i686-redhat-linux-gcc"
      "i686-redhat-linux-g++"
      "x86_64-w64-mingw32-gcc"
      "x86_64-w64-mingw32-g++"
      "i686-w64-mingw32-gcc"
      "i686-w64-mingw32-g++"
      "x86_64-apple-darwin-clang"
      "x86_64-apple-darwin-clang++"
      "aarch64-apple-darwin-clang"
      "aarch64-apple-darwin-clang++"
      # python artifacts: dockcross crosscompilers
      "aarch64-unknown-linux-gnueabi-gcc"
      "aarch64-unknown-linux-gnueabi-g++"
      "armv7-unknown-linux-gnueabi-gcc"
      "armv7-unknown-linux-gnueabi-g++"
    )
    CCACHE_BINARY_PATH="$(command -v ccache)"
    TEMP_CCACHE_BINDIR="$(mktemp -d)"

    for compiler in "${SUPPORTED_COMPILERS[@]}"
    do
      # create a symlink pointing to ccache if compiler binary exists
      if [ -x "$(command -v $compiler)" ]
      then
        ln -s "${CCACHE_BINARY_PATH}" "${TEMP_CCACHE_BINDIR}/${compiler}"
        echo "Creating symlink $compiler pointing to ${CCACHE_BINARY_PATH}"
      fi
    done
    echo "Adding ${TEMP_CCACHE_BINDIR} to PATH"
    export PATH="${TEMP_CCACHE_BINDIR}:$PATH"
  fi
fi
