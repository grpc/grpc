# Copyright 2017, Google Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

licenses(["notice"])  # 3-clause BSD

load("//bazel:grpc_build_system.bzl", "grpc_cc_library", "grpc_cc_binary", "grpc_cc_test")

package(
    default_visibility = ["//visibility:public"],
    features = [
        "-layering_check",
        "-parse_headers",
    ],
)

# The following builds a shared-object to confirm that grpc++_unsecure
# builds properly. Build-only is sufficient here
grpc_cc_binary(
    name = "testso.so",
    srcs = [],
    linkshared = 1,
    deps = ["//:grpc++_unsecure"],
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

grpc_cc_library(
    name = "test_util",
    srcs = [
        "byte_buffer_proto_helper.cc",
        "create_test_channel.cc",
        "string_ref_helper.cc",
        "subprocess.cc",
        "test_credentials_provider.cc",
    ],
    hdrs = [
        "byte_buffer_proto_helper.h",
        "create_test_channel.h",
        "string_ref_helper.h",
        "subprocess.h",
        "test_credentials_provider.h",
    ],
    deps = [
        "//:grpc++",
        "//test/core/end2end:ssl_test_data",
        "//test/core/util:gpr_test_util",
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
