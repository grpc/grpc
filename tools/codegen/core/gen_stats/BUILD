# Copyright 2025 gRPC authors.
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

load("//bazel:grpc_build_system.bzl", "grpc_cc_binary", "grpc_cc_library", "grpc_cc_test")

package(features = [
    "layering_check",
    "parse_headers",
])

grpc_cc_library(
    name = "gen_stats_utils",
    srcs = ["gen_stats_utils.cc"],
    hdrs = ["gen_stats_utils.h"],
    external_deps = [
        "absl/log",
        "absl/log:check",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
        "absl/time:time",
        "absl/container:flat_hash_map",
        "@yaml-cpp//:yaml-cpp",
    ],
    visibility = ["//visibility:public"],
)

grpc_cc_binary(
    name = "gen_stats_data",
    srcs = ["gen_stats_data.cc"],
    external_deps = [
        "absl/flags:flag",
        "absl/flags:parse",
        "absl/log",
        "absl/log:check",
        "absl/status",
        "absl/status:statusor",
    ],
    visibility = ["//visibility:public"],
    deps = [":gen_stats_utils"],
)

grpc_cc_test(
    name = "gen_stats_utils_test",
    srcs = ["gen_stats_utils_test.cc"],
    external_deps = [
        "gtest",
        "absl/status",
        "absl/strings",
    ],
    deps = [
        ":gen_stats_utils",
        "//test/core/test_util:grpc_test_util",
        "//test/cpp/util:test_config",
    ],
)
