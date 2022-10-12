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

#
# This is for the gRPC build system. This isn't intended to be used outsite of
# the BUILD file for gRPC. It contains the mapping for the template system we
# use to generate other platform's build system files.
#
# Please consider that there should be a high bar for additions and changes to
# this file.
# Each rule listed must be re-written for Google's internal build system, and
# each change must be ported from one to the other.
#

load("@build_bazel_rules_apple//apple:ios.bzl", "ios_unit_test")
load("@build_bazel_rules_apple//apple/testing/default_runner:ios_test_runner.bzl", "ios_test_runner")

ios_test_runner(
    name = "ios_x86_64_sim_runner",
    device_type = "iPhone 8",
)

objc_library(
    name = "LibuvTestsLib",
    testonly = True,
    srcs = glob(["*.m"]),
    deps = [
        "//external:libuv",
        "//external:libuv_test",
    ],
)

ios_unit_test(
    name = "LibuvTest",
    minimum_os_version = "9.0",
    runner = ":ios_x86_64_sim_runner",
    deps = [":LibuvTestsLib"],
)
