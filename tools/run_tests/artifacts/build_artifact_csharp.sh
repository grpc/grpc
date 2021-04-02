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

mkdir -p cmake/build
cd cmake/build

cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo \
      -DgRPC_BACKWARDS_COMPATIBILITY_MODE=ON \
      -DgRPC_BUILD_TESTS=OFF \
      ../..

make grpc_csharp_ext -j2

if [ -f "libgrpc_csharp_ext.so" ]
then
    # in case we are in a crosscompilation environment
    STRIP=${STRIP:-strip}
    OBJCOPY=${OBJCOPY:-objcopy}
    
    # The .so file with all debug symbols is too large to
    # package in the default nuget package.
    # But we still want to keep the version with symbols
    # to include it in a special "debug" package.
    cp libgrpc_csharp_ext.so libgrpc_csharp_ext.dbginfo.so
    ${STRIP} --strip-unneeded libgrpc_csharp_ext.so
    ${OBJCOPY} --add-gnu-debuglink=libgrpc_csharp_ext.dbginfo.so libgrpc_csharp_ext.so
fi

cd ../..

mkdir -p "${ARTIFACTS_OUT}"
cp cmake/build/libgrpc_csharp_ext.so cmake/build/libgrpc_csharp_ext.dbginfo.so "${ARTIFACTS_OUT}" || cp cmake/build/libgrpc_csharp_ext.dylib "${ARTIFACTS_OUT}"
