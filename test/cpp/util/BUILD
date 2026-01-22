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

load("//bazel:grpc_build_system.bzl", "grpc_cc_binary", "grpc_cc_library", "grpc_cc_test", "grpc_package")

licenses(["notice"])

grpc_package(
    name = "test/cpp/util",
    features = [
        "layering_check",
        "parse_headers",
    ],
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
        "absl/flags:parse",
    ],
    deps = ["//:gpr"],
)

grpc_cc_library(
    name = "grpc++_proto_reflection_desc_db",
    srcs = [
        "proto_reflection_descriptor_database.cc",
    ],
    hdrs = [
        "proto_reflection_descriptor_database.h",
    ],
    external_deps = [
        "absl/log:log",
        "absl/container:flat_hash_map",
        "absl/container:flat_hash_set",
    ],
    deps = [
        "//:gpr",
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
    "credentials.h",
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
        "absl/flags:flag",
        "absl/log:check",
        "absl/log:log",
        "absl/memory",
        "protobuf",
    ],
    deps = [
        "//:gpr",
        "//:grpc++",
        "//:grpc++_config_proto",
        "//:subprocess",
        "//src/core:grpc_check",
        "//src/core:grpc_fake_credentials",
        "//test/core/end2end:ssl_test_data",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
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
    external_deps = [
        "gtest",
        "protobuf",
        "absl/strings",
    ],
    deps = [
        "//:channelz",
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//:grpc++_config_proto",
        "//src/core:error",
        "//src/core:json",
        "//src/core:json_reader",
        "//src/core:json_writer",
        "//src/proto/grpc/channelz:channelz_proto",
        "//src/proto/grpc/channelz:channelz_proto_cc_proto",
    ],
)

grpc_cc_library(
    name = "test_util_unsecure",
    srcs = GRPCXX_TESTUTIL_SRCS,
    hdrs = GRPCXX_TESTUTIL_HDRS,
    external_deps = [
        "protobuf",
        "absl/memory",
    ],
    deps = [
        "//:grpc++_config_proto",
        "//:grpc++_unsecure",
        "//:subprocess",
        "//src/core:grpc_fake_credentials",
        "//test/core/test_util:grpc_test_util_base",
        "//test/core/test_util:grpc_test_util_unsecure",
    ],
)

grpc_cc_library(
    name = "grpc_cli_utils",
    srcs = [
        "cli_call.cc",
        "cli_credentials.cc",
        "proto_file_parser.cc",
        "service_describer.cc",
    ],
    hdrs = [
        "cli_call.h",
        "cli_credentials.h",
        "config_grpc_cli.h",
        "proto_file_parser.h",
        "service_describer.h",
    ],
    external_deps = [
        "absl/flags:flag",
        "absl/log:check",
        "absl/log:log",
        "protobuf",
        "protobuf_clib",
        "absl/strings",
        "absl/memory",
    ],
    deps = [
        ":grpc++_proto_reflection_desc_db",
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//:grpc++_config_proto",
        "//:grpc_slice",
        "//src/core:grpc_check",
        "//src/core:load_file",
        "//src/proto/grpc/reflection/v1alpha:reflection_proto",
        "@com_google_protobuf//src/google/protobuf/compiler:importer",
    ],
)

grpc_cc_library(
    name = "grpc_cli_libs",
    srcs = [
        "grpc_tool.cc",
    ],
    hdrs = [
        "grpc_tool.h",
    ],
    external_deps = [
        "absl/flags:flag",
        "absl/strings",
    ],
    deps = [
        ":grpc++_proto_reflection_desc_db",
        ":grpc_cli_utils",
        "//:gpr",
        "//:grpc",
        "//:grpc++",
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
    external_deps = ["absl/log:log"],
    deps = [
        "//:gpr",
        "//:grpc++",
        "//src/proto/grpc/testing:metrics_cc_grpc",
        "//src/proto/grpc/testing:metrics_cc_proto",
    ],
)

grpc_cc_test(
    name = "grpc_tool_test",
    srcs = [
        "grpc_tool_test.cc",
    ],
    data = [
        "//src/core/tsi/test_creds:ca.pem",
        "//src/core/tsi/test_creds:server1.key",
        "//src/core/tsi/test_creds:server1.pem",
        "//src/proto/grpc/testing:echo.proto",
        "//src/proto/grpc/testing:echo_messages.proto",
        "//src/proto/grpc/testing:simple_messages.proto",
    ],
    external_deps = [
        "absl/flags:flag",
        "gtest",
        "absl/strings",
        "absl/log:check",
    ],
    tags = [
        "grpc:internal-test-filter:-GrpcToolTest.CallCommandWithBadMetadata",
        "no_mac",
        "no_test_ios",
        "no_windows",
        "nomsan",  # death tests seem to be incompatible with msan
    ],
    deps = [
        ":grpc_cli_libs",
        ":grpc_cli_utils",
        ":test_util",
        "//:gpr",
        "//:grpc",
        "//:grpc++_base",
        "//:grpc++_reflection",
        "//:grpc_base",
        "//src/core:env",
        "//src/proto/grpc/testing:echo_cc_grpc",
        "//src/proto/grpc/testing:echo_cc_proto",
        "//src/proto/grpc/testing:echo_messages_cc_proto",
        "//src/proto/grpc/testing:simple_messages_cc_proto",
        "//test/core/test_util:grpc_test_util",
        "//test/cpp/util:test_config",
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
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        ":test_util",
        "//:grpc",
        "//:grpc++",
        "//:grpc++_base",
        "//:grpc_base",
        "//:grpc_slice",
        "//test/core/test_util:grpc_test_util",
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
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        ":test_util",
        "//:grpc",
        "//:grpc++",
        "//:grpc++_base",
        "//:grpc_base",
        "//:grpc_slice",
        "//test/core/test_util:grpc_test_util",
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
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:grpc++",
        "//test/core/test_util:grpc_test_util",
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
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        ":test_config",
        ":test_util",
        "//:gpr",
        "//:grpc++",
        "//:grpc++_base",
        "//test/core/test_util:grpc_test_util",
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
        ":grpc_cli_utils",
        ":test_util",
        "//:grpc",
        "//:grpc++",
        "//:grpc++_base",
        "//:grpc_base",
        "//src/proto/grpc/testing:echo_cc_grpc",
        "//test/core/test_util:grpc_test_util",
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
    tags = [
        "grpc:fails-internally",
        "no_test_ios",
    ],
    deps = [
        "//:grpc++_error_details",
        "//src/proto/grpc/testing:echo_messages_cc_proto",
        "//test/core/test_util:grpc_test_util",
        "@com_google_googleapis//google/rpc:status_cc_proto",
    ],
)

grpc_cc_binary(
    name = "grpc_cli",
    srcs = [
        "grpc_cli.cc",
    ],
    external_deps = [
        "absl/flags:flag",
    ],
    visibility = [
        "//bazel:cli",
    ],
    deps = [
        ":grpc++_proto_reflection_desc_db",
        ":grpc_cli_libs",
        ":grpc_cli_utils",
        ":test_config",
        "//:grpc++",
        "//src/proto/grpc/reflection/v1alpha:reflection_proto",
    ],
)

grpc_cc_binary(
    name = "channelz_sampler",
    srcs = ["channelz_sampler.cc"],
    external_deps = [
        "absl/flags:flag",
        "absl/log:check",
        "absl/log:log",
        "absl/strings",
        "absl/strings:str_format",
        "protobuf",
    ],
    tags = [
        "no_windows",  # unistd.h
    ],
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//:grpcpp_channelz",
        "//src/core:grpc_check",
        "//src/core:json",
        "//src/core:json_writer",
        "//src/proto/grpc/channelz:channelz_proto",
        "//src/proto/grpc/channelz:channelz_proto_cc_proto",
        "//test/core/test_util:grpc_test_util",
        "//test/cpp/util:test_config",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_test(
    name = "channelz_sampler_test",
    srcs = [
        "channelz_sampler_test.cc",
    ],
    data = [
        ":channelz_sampler",
    ],
    external_deps = [
        "absl/log:check",
        "absl/log:log",
        "absl/strings",
        "gtest",
    ],
    flaky = True,
    tags = [
        # TODO(b/166166195) The following file cannot pass internally
        # because of include issues.
        "grpc:broken-internally",
        "no_mac",  # cmake does not build channelz_sampler in Basic Tests C/C++ MacOS test
        "no_test_android",  # android_cc_test doesn't work with data dependency.
        "no_test_ios",
        "no_windows",  # unistd.h
    ],
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//:grpcpp_channelz",
        "//src/core:env",
        "//src/core:grpc_check",
        "//src/proto/grpc/channelz:channelz_proto",
        "//src/proto/grpc/testing:test_cc_grpc",
        "//test/core/test_util:grpc_test_util",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_library(
    name = "tls_test_utils",
    srcs = [
        "tls_test_utils.cc",
    ],
    hdrs = [
        "tls_test_utils.h",
    ],
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "proto_buffer_reader_test",
    srcs = [
        "proto_buffer_reader_test.cc",
    ],
    external_deps = [
        "gtest",
    ],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        ":test_util",
        "//:grpc++",
        "//:grpc++_base",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "proto_buffer_writer_test",
    srcs = [
        "proto_buffer_writer_test.cc",
    ],
    external_deps = [
        "gtest",
    ],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        ":test_util",
        "//:grpc++",
        "//:grpc++_base",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_library(
    name = "get_grpc_test_runfile_dir",
    srcs = [
        "get_grpc_test_runfile_dir.cc",
    ],
    hdrs = [
        "get_grpc_test_runfile_dir.h",
    ],
    external_deps = [
        "absl/strings",
    ],
    deps = [
        "//src/core:env",
    ],
)
