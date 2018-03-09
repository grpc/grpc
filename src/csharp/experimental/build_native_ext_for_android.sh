#!/bin/sh
# Copyright 2018 gRPC authors.
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

cd "$(dirname "$0")/../../../cmake"

mkdir -p build
cd build

# set to the location where Android SDK is installed
ANDROID_NDK_PATH="$HOME/android-ndk-r16b"

cmake ../.. \
  -DCMAKE_SYSTEM_NAME=Android \
  -DCMAKE_SYSTEM_VERSION=15 \
  -DCMAKE_ANDROID_ARCH_ABI=armeabi-v7a \
  -DCMAKE_ANDROID_NDK="${ANDROID_NDK_PATH}" \
  -DCMAKE_ANDROID_STL_TYPE=c++_static \
  -DRUN_HAVE_POSIX_REGEX=0 \
  -DRUN_HAVE_STD_REGEX=0 \
  -DRUN_HAVE_STEADY_CLOCK=0 \
  -DCMAKE_BUILD_TYPE=Release

make -j4 grpc_csharp_ext
