#!/bin/bash
# Copyright 2022 gRPC authors.
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

cd $(dirname $0)/../..

mkdir clang_msan

# The directory we will install the clang toolchain to
TARGET="`pwd`/tools/local_msan/clang_msan/"

cd $(mktemp -d)

# Commands borrowed from https://skia.googlesource.com/skia/+/main/infra/bots/assets/clang_linux/create.py
git clone --depth 1 -b release/13.x https://llvm.googlesource.com/llvm-project
cd llvm-project/
mkdir out
cd out/
cmake ../llvm/ -G Ninja -DCMAKE_BUILD_TYPE=MinSizeRel -DCMAKE_INSTALL_PREFIX=$TARGET -DLLVM_ENABLE_PROJECTS='clang;clang-tools-extra;compiler-rt;libcxx;libcxxabi;lld' -DLLVM_INSTALL_TOOLCHAIN_ONLY=ON -DLLVM_ENABLE_TERMINFO=OFF
ninja install
cp bin/llvm-symbolizer $TARGET/bin
cp bin/llvm-profdata $TARGET/bin
cp bin/llvm-cov $TARGET/bin
cp `$TARGET/bin/clang++ -print-file-name=libstdc++.so.6` $TARGET/lib
mkdir ../msan_out
cd ../msan_out/
cmake ../llvm -G Ninja -DCMAKE_BUILD_TYPE=MinSizeRel -DCMAKE_C_COMPILER=$TARGET/bin/clang -DCMAKE_CXX_COMPILER=$TARGET/bin/clang++ -DLLVM_ENABLE_PROJECTS='libcxx;libcxxabi' -DLLVM_USE_SANITIZER=MemoryWithOrigins
ninja cxx
cp -r lib $TARGET/msan

