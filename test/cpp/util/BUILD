# Copyright 2017 gRPC authors.
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

licenses(["notice"])  # Apache v2

load("//bazel:grpc_build_system.bzl", "grpc_cc_library", "grpc_cc_binary", "grpc_cc_test", "grpc_package")

grpc_package(
    name = "test/cpp/util",
    visibility = "public",
)

grpc_cc_library(
    name = "test_config",
    srcs = [
        "test_config_cc.cc",
    ],
    hdrs = [
        "test_config.h",
    ],
    external_deps = [
        "gflags",
    ],
    deps = [
        "//:gpr",
    ],
)

grpc_cc_library(
    name = "grpc++_proto_reflection_desc_db",
    srcs = [
        "proto_reflection_descriptor_database.cc",
    ],
    hdrs = [
        "proto_reflection_descriptor_database.h",
    ],
    deps = [
        "//:grpc++",
        "//:grpc++_config_proto",
        "//src/proto/grpc/reflection/v1alpha:reflection_proto",
    ],
)

GRPCXX_TESTUTIL_SRCS = [
    "byte_buffer_proto_helper.cc",
    "string_ref_helper.cc",
    "subprocess.cc",
]

GRPCXX_TESTUTIL_HDRS = [
    "byte_buffer_proto_helper.h",
    "string_ref_helper.h",
    "subprocess.h",
]

grpc_cc_library(
    name = "test_util",
    srcs = GRPCXX_TESTUTIL_SRCS + [
        "create_test_channel.cc",
        "test_credentials_provider.cc",
    ],
    hdrs = GRPCXX_TESTUTIL_HDRS + [
        "create_test_channel.h",
        "test_credentials_provider.h",
    ],
    external_deps = [
        "protobuf",
    ],
    deps = [
        "//:grpc++",
        "//test/core/end2end:ssl_test_data",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_library(
    name = "channel_trace_proto_helper",
    testonly = 1,
    srcs = [
        "channel_trace_proto_helper.cc",
    ],
    hdrs = [
        "channel_trace_proto_helper.h",
    ],
    deps = [
        "//:grpc++",
        "//src/proto/grpc/channelz:channelz_proto",
    ],
    external_deps = [
        "gtest",
        "protobuf",
    ],
)

grpc_cc_library(
    name = "test_util_unsecure",
    srcs = GRPCXX_TESTUTIL_SRCS,
    hdrs = GRPCXX_TESTUTIL_HDRS,
    external_deps = [
        "protobuf",
    ],
    deps = [
        "//:grpc++_unsecure",
        "//test/core/util:grpc_test_util_unsecure",
    ],
)

grpc_cc_library(
    name = "grpc_cli_libs",
    srcs = [
        "cli_call.cc",
        "cli_credentials.cc",
        "grpc_tool.cc",
        "proto_file_parser.cc",
        "service_describer.cc",
    ],
    hdrs = [
        "cli_call.h",
        "cli_credentials.h",
        "config_grpc_cli.h",
        "grpc_tool.h",
        "proto_file_parser.h",
        "service_describer.h",
    ],
    external_deps = [
        "gflags",
        "protobuf",
        "protobuf_clib",
    ],
    deps = [
        ":grpc++_proto_reflection_desc_db",
        "//:grpc++",
        "//src/proto/grpc/reflection/v1alpha:reflection_proto",
    ],
)

grpc_cc_library(
    name = "metrics_server_lib",
    srcs = [
        "metrics_server.cc",
    ],
    hdrs = [
        "metrics_server.h",
    ],
    deps = [
        "//:grpc++",
        "//src/proto/grpc/testing:metrics_proto",
    ],
)

grpc_cc_test(
    name = "grpc_tool_test",
    srcs = [
        "grpc_tool_test.cc",
    ],
    data = [
        "//src/proto/grpc/testing:echo.proto",
        "//src/proto/grpc/testing:echo_messages.proto",
    ],
    external_deps = [
        "gtest",
    ],
    deps = [
        ":grpc_cli_libs",
        ":test_util",
        "//:grpc++_reflection",
        "//src/proto/grpc/testing:echo_messages_proto",
        "//src/proto/grpc/testing:echo_proto",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "byte_buffer_test",
    srcs = [
        "byte_buffer_test.cc",
    ],
    external_deps = [
        "gtest",
    ],
    deps = [
        ":test_util",
    ],
)

grpc_cc_test(
    name = "slice_test",
    srcs = [
        "slice_test.cc",
    ],
    external_deps = [
        "gtest",
    ],
    deps = [
        ":test_util",
    ],
)

grpc_cc_test(
    name = "string_ref_test",
    srcs = [
        "string_ref_test.cc",
    ],
    external_deps = [
        "gtest",
    ],
    deps = [
        "//:grpc++",
    ],
)

grpc_cc_test(
    name = "time_test",
    srcs = [
        "time_test.cc",
    ],
    external_deps = [
        "gtest",
    ],
    deps = [
        ":test_util",
    ],
)

grpc_cc_test(
    name = "cli_call_test",
    srcs = [
        "cli_call_test.cc",
    ],
    external_deps = [
        "gtest",
    ],
    deps = [
        ":grpc_cli_libs",
        ":test_util",
        "//src/proto/grpc/testing:echo_proto",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "error_details_test",
    srcs = [
        "error_details_test.cc",
    ],
    external_deps = [
        "gtest",
    ],
    deps = [
        "//:grpc++_error_details",
        "//src/proto/grpc/testing:echo_messages_proto",
    ],
)

grpc_cc_binary(
    name = "grpc_cli",
    srcs = [
        "cli_call.cc",
        "cli_call.h",
        "cli_credentials.cc",
        "cli_credentials.h",
        "config_grpc_cli.h",
        "grpc_cli.cc",
        "grpc_tool.cc",
        "grpc_tool.h",
        "proto_file_parser.cc",
        "proto_file_parser.h",
        "proto_reflection_descriptor_database.cc",
        "proto_reflection_descriptor_database.h",
        "service_describer.cc",
        "service_describer.h",
        "test_config.h",
        "test_config_cc.cc",
    ],
    external_deps = [
        "gflags",
    ],
    deps = [
        "//:grpc++",
        "//src/proto/grpc/reflection/v1alpha:reflection_proto",
    ],
)
