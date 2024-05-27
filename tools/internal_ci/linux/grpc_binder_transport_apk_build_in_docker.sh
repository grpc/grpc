#!/usr/bin/env bash
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

set -ex

echo $ANDROID_HOME
echo $ANDROID_NDK_HOME

# Android platforms only works with Bazel >= 7.0
export OVERRIDE_BAZEL_VERSION=7.1.0

python3 tools/run_tests/python_utils/bazel_report_helper.py --report_path bazel_binder_example_app
bazel_binder_example_app/bazel_wrapper \
  --bazelrc=tools/remote_build/include/test_locally_with_resultstore_results.bazelrc \
  build \
  --extra_toolchains=@androidndk//:all \
  --android_platforms=//:android_x86_64,//:android_armv7,//:android_arm64 \
  //examples/android/binder/java/io/grpc/binder/cpp/exampleclient:app \
  //examples/android/binder/java/io/grpc/binder/cpp/exampleserver:app

# Make sure the Java code that will be invoked by binder transport
# implementation builds
python3 tools/run_tests/python_utils/bazel_report_helper.py --report_path bazel_binder_connection_helper
bazel_binder_connection_helper/bazel_wrapper \
  --bazelrc=tools/remote_build/include/test_locally_with_resultstore_results.bazelrc \
  build \
  --define=use_strict_warning=true \
  @binder_transport_android_helper//io/grpc/binder/cpp:connection_helper
