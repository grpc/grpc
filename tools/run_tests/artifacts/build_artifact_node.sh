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

NODE_TARGET_ARCH=$1
NODE_TARGET_OS=$2
source ~/.nvm/nvm.sh

nvm use 8
set -ex

cd $(dirname $0)/../../..

rm -rf build || true

mkdir -p "${ARTIFACTS_OUT}"

npm update

node_versions=( 4.0.0 5.0.0 6.0.0 7.0.0 8.0.0 )

electron_versions=( 1.0.0 1.1.0 1.2.0 1.3.0 1.4.0 1.5.0 1.6.0 )

for version in ${node_versions[@]}
do
  ./node_modules/.bin/node-pre-gyp configure rebuild package --target=$version --target_arch=$NODE_TARGET_ARCH --grpc_alpine=true
  cp -r build/stage/* "${ARTIFACTS_OUT}"/
  if [ "$NODE_TARGET_ARCH" == 'x64' ] && [ "$NODE_TARGET_OS" == 'linux' ]
  then
    # Cross compile for ARM on x64
    CC=arm-linux-gnueabihf-gcc CXX=arm-linux-gnueabihf-g++ LD=arm-linux-gnueabihf-g++ ./node_modules/.bin/node-pre-gyp configure rebuild package testpackage --target=$version --target_arch=arm
    cp -r build/stage/* "${ARTIFACTS_OUT}"/
  fi
done

for version in ${electron_versions[@]}
do
  HOME=~/.electron-gyp ./node_modules/.bin/node-pre-gyp configure rebuild package --runtime=electron --target=$version --target_arch=$NODE_TARGET_ARCH --disturl=https://atom.io/download/electron
  cp -r build/stage/* "${ARTIFACTS_OUT}"/
done
