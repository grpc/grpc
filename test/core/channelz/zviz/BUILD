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

load("//bazel:grpc_build_system.bzl", "grpc_cc_library", "grpc_cc_test", "grpc_package")
load("//test/core/test_util:grpc_fuzzer.bzl", "grpc_fuzz_test")

grpc_package(name = "test/core/channelz/zviz")

licenses(["notice"])

grpc_fuzz_test(
    name = "strings_test",
    srcs = ["strings_test.cc"],
    external_deps = [
        "absl/log:check",
        "fuzztest",
        "fuzztest_main",
    ],
    deps = [
        "//src/core:zviz_strings",
    ],
)

grpc_cc_library(
    name = "environment_fake",
    testonly = True,
    hdrs = ["environment_fake.h"],
    external_deps = [
        "absl/container:flat_hash_map",
    ],
    deps = [
        "//src/core:zviz_environment",
    ],
)

grpc_fuzz_test(
    name = "environment_test",
    srcs = ["environment_test.cc"],
    external_deps = [
        "absl/log:check",
        "fuzztest",
        "fuzztest_main",
    ],
    deps = [
        "environment_fake",
        "//src/core:zviz_environment",
    ],
)

grpc_cc_test(
    name = "layout_test",
    srcs = ["layout_test.cc"],
    external_deps = [
        "gtest",
        "gtest_main",
    ],
    deps = [
        "//src/core:zviz_layout",
    ],
)
