#!/usr/bin/env bash
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

set -ex

# tests require port server to be running
python3 tools/run_tests/start_port_server.py

# test gRPC C/C++ with bazel
python3 tools/run_tests/python_utils/bazel_report_helper.py --report_path bazel_test_c_cpp
bazel_test_c_cpp/bazel_wrapper \
  --bazelrc=tools/remote_build/include/test_locally_with_resultstore_results.bazelrc \
  test --config=opt \
  --test_tag_filters=-no_linux,-no_arm64 \
  --build_tag_filters=-no_linux,-no_arm64 \
  -- \
  //test/...
