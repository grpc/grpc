# Copyright 2021 gRPC authors.
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

#!/usr/bin/env bash
#
# NOTE: No empty lines should appear in this file before igncr is set!
set -ex -o igncr || set -ex

mkdir -p /var/local/git
git clone /var/local/jenkins/grpc /var/local/git/grpc
(cd /var/local/jenkins/grpc/ && git submodule foreach 'cd /var/local/git/grpc \
&& git submodule update --init --reference /var/local/jenkins/grpc/${name} \
${name}')
cd /var/local/git/grpc

echo $ANDROID_HOME
echo $ANDROID_NDK_HOME

# Build all targets using the strict warning option which leverages the
# clang compiler to check if sources can pass a set of warning options.
# CPU are specified because gRPC does not build with 32bit NDK (which has socklen_t
# defined as int due to an accident).
# The python option is for disabling python2 enforcement when packing APK
bazel build --define=use_strict_warning=true \
  --fat_apk_cpu=x86_64,arm64-v8a \
  --extra_toolchains=@rules_python//python:autodetecting_toolchain_nonstrict \
  //examples/android/binder/java/io/grpc/binder/cpp/example:app

# Make sure the Java code that will be invoked by binder transport
# implementation builds
bazel build --define=use_strict_warning=true \
  @binder_transport_android_helper//io/grpc/binder/cpp:connection_helper
