#!/bin/bash
# Copyright 2017 gRPC authors.
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

# Install openssl (to use instead of boringssl)
apt-get update && apt-get install -y libssl-dev

# Install CMake 3.16
apt-get update && apt-get install -y wget
wget -q -O cmake-linux.sh https://github.com/Kitware/CMake/releases/download/v3.16.1/cmake-3.16.1-Linux-x86_64.sh
sh cmake-linux.sh -- --skip-license --prefix=/usr
rm cmake-linux.sh

# Build and install gRPC for the host architecture.
# We do this because we need to be able to run protoc and grpc_cpp_plugin
# while cross-compiling.
mkdir -p "cmake/build"
pushd "cmake/build"
cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DgRPC_INSTALL=ON \
  -DgRPC_BUILD_TESTS=OFF \
  -DgRPC_SSL_PROVIDER=package \
  ../..
make -j4 install
popd

# Download raspberry pi toolchain.
mkdir -p "/tmp/raspberrypi_root"
pushd "/tmp/raspberrypi_root"
git clone https://github.com/raspberrypi/tools raspberrypi-tools
cd raspberrypi-tools && git checkout 4a335520900ce55e251ac4f420f52bf0b2ab6b1f && cd ..

# Write a toolchain file to use for cross-compiling.
cat > toolchain.cmake <<'EOT'
SET(CMAKE_SYSTEM_NAME Linux)
SET(CMAKE_SYSTEM_PROCESSOR arm)
set(devel_root /tmp/raspberrypi_root)
set(CMAKE_STAGING_PREFIX ${devel_root}/stage)
set(tool_root ${devel_root}/raspberrypi-tools/arm-bcm2708)
set(CMAKE_SYSROOT ${tool_root}/arm-rpi-4.9.3-linux-gnueabihf/arm-linux-gnueabihf/sysroot)
set(CMAKE_C_COMPILER ${tool_root}/arm-linux-gnueabihf/bin/arm-linux-gnueabihf-gcc)
set(CMAKE_CXX_COMPILER ${tool_root}/arm-linux-gnueabihf/bin/arm-linux-gnueabihf-g++)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
EOT
popd

# Build and install gRPC for raspberry pi.
# This build will use the host architecture copies of protoc and
# grpc_cpp_plugin that we built earlier because we installed them
# to a location in our PATH (/usr/local/bin).
mkdir -p "cmake/raspberrypi_build"
pushd "cmake/raspberrypi_build"
cmake -DCMAKE_TOOLCHAIN_FILE=/tmp/raspberrypi_root/toolchain.cmake \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX=/tmp/raspberrypi_root/grpc_install \
      ../..
make -j4 install
popd

# Build helloworld example for raspberry pi.
# As above, it will find and use protoc and grpc_cpp_plugin
# for the host architecture.
mkdir -p "examples/cpp/helloworld/cmake/raspberrypi_build"
pushd "examples/cpp/helloworld/cmake/raspberrypi_build"
cmake -DCMAKE_TOOLCHAIN_FILE=/tmp/raspberrypi_root/toolchain.cmake \
      -DCMAKE_BUILD_TYPE=Release \
      -DProtobuf_DIR=/tmp/raspberrypi_root/stage/lib/cmake/protobuf \
      -DgRPC_DIR=/tmp/raspberrypi_root/stage/lib/cmake/grpc \
      ../..
make
popd
