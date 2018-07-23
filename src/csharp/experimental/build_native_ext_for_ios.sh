#!/bin/sh
# Copyright 2018 The gRPC Authors
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

# Helper script to crosscompile grpc_csharp_ext native extension for Android.

set -ex

cd "$(dirname "$0")/../../.."

# <iphoneos|iphonesimulator>
SDK="iphoneos"
# <arm64|x86_64|...>
ARCH="arm64"

PATH_AR="$(xcrun --sdk $SDK --find ar)"
PATH_CC="$(xcrun --sdk $SDK --find clang)"
PATH_CXX="$(xcrun --sdk $SDK --find clang++)"

# TODO(jtattermusch): add  -mios-version-min=6.0 and -Wl,ios_version_min=6.0
CPPFLAGS="-O2 -Wframe-larger-than=16384 -arch $ARCH -isysroot $(xcrun --sdk $SDK --show-sdk-path) -DPB_NO_PACKED_STRUCTS=1"
LDFLAGS="-arch $ARCH -isysroot $(xcrun --sdk $SDK --show-sdk-path)"

# TODO(jtattermusch): revisit the build arguments
make -j4 static_csharp \
    VALID_CONFIG_ios_$ARCH="1" \
    CC_ios_$ARCH="$PATH_CC" \
    CXX_ios_$ARCH="$PATH_CXX" \
    LD_ios_$ARCH="$PATH_CC" \
    LDXX_ios_$ARCH="$PATH_CXX" \
    CPPFLAGS_ios_$ARCH="$CPPFLAGS" \
    LDFLAGS_ios_$ARCH="$LDFLAGS" \
    DEFINES_ios_$ARCH="NDEBUG" \
    CONFIG="ios_$ARCH"
