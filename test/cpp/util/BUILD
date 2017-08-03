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

load("//bazel:grpc_build_system.bzl", "grpc_cc_library", "grpc_cc_binary", "grpc_cc_test")

package(
    default_visibility = ["//visibility:public"],
    features = [
        "-layering_check",
        "-parse_headers",
    ],
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
        "//:grpc++_config_proto",
        "//src/proto/grpc/reflection/v1alpha:reflection_proto",
    ],
)

GRPCXX_TESTUTIL_SRCS  = [
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
    deps = [
        "//:grpc++",
        "//test/core/end2end:ssl_test_data",
    ],
    external_deps = [
        "protobuf",
    ],
)

grpc_cc_library(
    name = "test_util_unsecure",
    srcs = GRPCXX_TESTUTIL_SRCS,
    hdrs = GRPCXX_TESTUTIL_HDRS,
    deps = [
        "//:grpc++_unsecure",
    ],
    external_deps = [
        "protobuf",
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
    deps = [
        "//:grpc++",
        "//src/proto/grpc/reflection/v1alpha:reflection_proto",
        ":grpc++_proto_reflection_desc_db",
    ],
    external_deps = [
        "gflags",
        "protobuf",
        "protobuf_clib",
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
        "//src/proto/grpc/testing:metrics_proto",
        "//:grpc++",
    ],
)

grpc_cc_test(
    name = "error_details_test",
    srcs = [
        "error_details_test.cc",
    ],
    deps = [
        "//:grpc++_error_details",
        "//src/proto/grpc/testing:echo_messages_proto",
    ],
    external_deps = [
        "gtest",
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
    deps = [
        "//:grpc++",
        "//src/proto/grpc/reflection/v1alpha:reflection_proto",
    ],
    external_deps = [
        "gflags",
    ],
)
