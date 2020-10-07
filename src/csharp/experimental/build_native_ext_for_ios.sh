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

# Helper script to crosscompile grpc_csharp_ext native extension for iOS.

set -ex

cd "$(dirname "$0")/../../.."

# Usage: build <iphoneos|iphonesimulator> <arm64|x86_64|...>
function build {
    SDK="$1"
    ARCH="$2"

    PATH_AR="$(xcrun --sdk $SDK --find ar)"
    PATH_CC="$(xcrun --sdk $SDK --find clang)"
    PATH_CXX="$(xcrun --sdk $SDK --find clang++)"

    CPPFLAGS="-O2 -Wframe-larger-than=16384 -arch $ARCH -isysroot $(xcrun --sdk $SDK --show-sdk-path) -mios-version-min=9.0 -DPB_NO_PACKED_STRUCTS=1"
    LDFLAGS="-arch $ARCH -isysroot $(xcrun --sdk $SDK --show-sdk-path) -Wl,ios_version_min=9.0"

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
}

# Usage: fatten <grpc_csharp_ext|...>
function fatten {
    LIB_NAME="$1"

    mkdir -p libs/ios
    lipo -create -output libs/ios/lib$LIB_NAME.a \
        libs/ios_armv7/lib$LIB_NAME.a \
        libs/ios_arm64/lib$LIB_NAME.a \
        libs/ios_x86_64/lib$LIB_NAME.a
}

build iphoneos armv7
build iphoneos arm64
build iphonesimulator x86_64

fatten grpc
fatten grpc_csharp_ext
