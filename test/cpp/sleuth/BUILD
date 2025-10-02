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

load("//bazel:grpc_build_system.bzl", "grpc_cc_binary", "grpc_cc_library", "grpc_cc_test", "grpc_package")

licenses(["notice"])

grpc_package(
    name = "test/cpp/sleuth",
    visibility = "public",
)

grpc_cc_library(
    name = "client",
    srcs = ["client.cc"],
    hdrs = ["client.h"],
    external_deps = [
        "absl/status:statusor",
    ],
    deps = [
        "//:grpc++",
        "//:grpcpp_latent_see_client",
        "//src/core:latent_see",
        "//src/proto/grpc/channelz/v2:latent_see_cc_grpc",
        "//src/proto/grpc/channelz/v2:service_cc_grpc",
    ],
)

grpc_cc_test(
    name = "client_test",
    srcs = ["client_test.cc"],
    external_deps = [
        "gtest",
        "gtest_main",
    ],
    deps = [
        ":client",
        "//:grpcpp_channelz",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_library(
    name = "tool_test",
    testonly = True,
    srcs = ["tool_test.cc"],
    hdrs = ["tool_test.h"],
    external_deps = [
        "absl/status:statusor",
        "absl/strings",
    ],
    deps = [
        ":tool",
    ],
)

grpc_cc_test(
    name = "tool_test_test",
    srcs = ["tool_test_test.cc"],
    external_deps = [
        "gtest",
        "gtest_main",
    ],
    deps = [
        ":tool_test",
    ],
)

grpc_cc_test(
    name = "channelz_tool_test",
    srcs = ["channelz_tool_test.cc"],
    external_deps = [
        "absl/flags:flag",
        "gtest",
        "gtest_main",
    ],
    deps = [
        ":channelz_tool",
        ":tool_test",
        "//:grpc++_public_hdrs",
        "//:grpcpp_channelz",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "latent_see_tool_test",
    srcs = ["latent_see_tool_test.cc"],
    external_deps = [
        "absl/flags:flag",
        "gtest",
        "gtest_main",
    ],
    deps = [
        ":latent_see_tool",
        ":tool_test",
        "//:grpc++_public_hdrs",
        "//:grpcpp_latent_see_service",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_binary(
    name = "sleuth",
    srcs = ["sleuth.cc"],
    external_deps = [
        "absl/flags:parse",
        "absl/status",
    ],
    visibility = [
        "//bazel:sleuth",
    ],
    deps = [
        ":channelz_tool",
        ":latent_see_tool",
        ":tool",
        "//test/cpp/util:test_config",
    ],
)

grpc_cc_library(
    name = "tool",
    hdrs = ["tool.h"],
    external_deps = [
        "absl/container:flat_hash_map",
        "absl/functional:any_invocable",
        "absl/log:check",
        "absl/status",
        "absl/strings:string_view",
    ],
    deps = [
        "//:grpc_base",
    ],
)

grpc_cc_library(
    name = "channelz_tool",
    srcs = ["channelz_tool.cc"],
    external_deps = [
        "absl/status:status",
    ],
    deps = [
        ":client",
        ":tool",
        ":tool_options",
        "//:grpc_base",
        "//src/core:zviz_entity",
        "//src/core:zviz_format_entity_list",
        "//src/core:zviz_layout_text",
        "//test/cpp/util:grpc_cli_utils",
    ],
    alwayslink = True,
)

grpc_cc_library(
    name = "latent_see_tool",
    srcs = ["latent_see_tool.cc"],
    external_deps = [
        "absl/status:status",
    ],
    deps = [
        ":client",
        ":tool",
        ":tool_options",
        "//:grpc_base",
        "//src/core:latent_see",
    ],
    alwayslink = True,
)

grpc_cc_library(
    name = "tool_credentials",
    srcs = ["tool_credentials.cc"],
    hdrs = ["tool_credentials.h"],
    external_deps = [
        "absl/status:status",
    ],
    deps = [
        "//:grpc++",
        "//test/cpp/util:grpc_cli_utils",
    ],
)

grpc_cc_library(
    name = "tool_options",
    srcs = ["tool_options.cc"],
    hdrs = ["tool_options.h"],
    external_deps = [
        "absl/flags:flag",
    ],
    deps = [
        ":client",
        ":tool_credentials",
        "//src/core:chaotic_good",
    ],
)
