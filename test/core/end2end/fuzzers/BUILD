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

load("//bazel:grpc_build_system.bzl", "grpc_cc_library", "grpc_cc_proto_library", "grpc_internal_proto_library", "grpc_package")
load("//test/core/test_util:grpc_fuzzer.bzl", "grpc_fuzz_test")

grpc_package(
    name = "test/core/end2end/fuzzers",
    features = [
        "layering_check",
        "parse_headers",
    ],
)

licenses(["notice"])

grpc_internal_proto_library(
    name = "api_fuzzer_proto",
    srcs = ["api_fuzzer.proto"],
    deps = [
        "//test/core/event_engine/fuzzing_event_engine:fuzzing_event_engine_proto",
        "//test/core/test_util:fuzz_config_vars_proto",
        "//test/core/test_util:fuzzing_channel_args_proto",
    ],
)

grpc_cc_proto_library(
    name = "api_fuzzer_cc_proto",
    deps = ["api_fuzzer_proto"],
)

grpc_cc_library(
    name = "fuzzing_common",
    testonly = True,
    srcs = ["fuzzing_common.cc"],
    hdrs = ["fuzzing_common.h"],
    external_deps = [
        "absl/log:check",
        "absl/log:log",
        "absl/strings",
        "absl/types:span",
    ],
    deps = [
        "api_fuzzer_cc_proto",
        "//:channel",
        "//:event_engine_base_hdrs",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc",
        "//:iomgr_timer",
        "//:ref_counted_ptr",
        "//src/core:default_event_engine",
        "//src/core:experiments",
        "//src/core:grpc_check",
        "//src/core:memory_quota",
        "//src/core:postmortem",
        "//src/core:resource_quota",
        "//src/core:slice",
        "//src/core:time",
        "//src/core:useful",
        "//test/core/event_engine/fuzzing_event_engine",
        "//test/core/event_engine/fuzzing_event_engine:fuzzing_event_engine_cc_proto",
        "//test/core/test_util:postmortem",
    ],
)

grpc_fuzz_test(
    name = "api_fuzzer",
    srcs = ["api_fuzzer.cc"],
    external_deps = [
        "absl/log:check",
        "fuzztest",
        "fuzztest_main",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/time",
    ],
    tags = [
        "no_mac",
        "no_windows",
    ],
    deps = [
        "api_fuzzer_cc_proto",
        "fuzzing_common",
        "//:debug_location",
        "//:endpoint_addresses",
        "//:event_engine_base_hdrs",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc",
        "//:grpc_core_credentials_header",
        "//:grpc_resolver_dns_ares",
        "//:iomgr",
        "//:parse_address",
        "//:ref_counted_ptr",
        "//src/core:channel_args",
        "//src/core:closure",
        "//src/core:default_event_engine",
        "//src/core:env",
        "//src/core:experiments",
        "//src/core:grpc_check",
        "//src/core:grpc_transport_inproc",
        "//src/core:iomgr_fwd",
        "//src/core:resolved_address",
        "//src/core:slice",
        "//src/core:time",
        "//test/core/end2end:ssl_test_data",
        "//test/core/event_engine/fuzzing_event_engine",
        "//test/core/event_engine/fuzzing_event_engine:fuzzing_event_engine_cc_proto",
        "//test/core/test_util:fuzz_config_vars",
        "//test/core/test_util:fuzz_config_vars_helpers",
        "//test/core/test_util:fuzzing_channel_args",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
        "@com_google_protobuf//:protobuf",
    ],
)

grpc_internal_proto_library(
    name = "fuzzer_input_proto",
    srcs = ["fuzzer_input.proto"],
    deps = [
        "api_fuzzer_proto",
        "//src/core:chaotic_good_frame_proto",
        "//test/core/event_engine/fuzzing_event_engine:fuzzing_event_engine_proto",
        "//test/core/test_util:fuzz_config_vars_proto",
        "//test/core/test_util:fuzzing_channel_args_proto",
    ],
)

grpc_cc_proto_library(
    name = "fuzzer_input_cc_proto",
    deps = ["fuzzer_input_proto"],
)

grpc_cc_library(
    name = "network_input",
    testonly = True,
    srcs = ["network_input.cc"],
    hdrs = ["network_input.h"],
    external_deps = [
        "absl/log:log",
        "absl/strings",
        "absl/types:span",
    ],
    deps = [
        "fuzzer_input_cc_proto",
        "//:chttp2_frame",
        "//:chttp2_varint",
        "//:config",
        "//:exec_ctx",
        "//:iomgr",
        "//src/core:channel_args",
        "//src/core:channel_args_endpoint_config",
        "//src/core:channel_args_preconditioning",
        "//src/core:chaotic_good_frame_header",
        "//src/core:chaotic_good_tcp_frame_transport",
        "//src/core:event_engine_tcp_socket_utils",
        "//src/core:slice",
        "//src/core:slice_buffer",
        "//src/core:slice_refcount",
        "//src/core:time",
        "//src/core:useful",
        "//test/core/event_engine/fuzzing_event_engine",
        "//test/core/test_util:fuzzing_channel_args",
        "//test/core/test_util:grpc_test_util_base",
    ],
)

grpc_fuzz_test(
    name = "client_fuzzer",
    srcs = ["client_fuzzer.cc"],
    external_deps = [
        "absl/log:check",
        "fuzztest",
        "fuzztest_main",
        "absl/status:statusor",
    ],
    tags = ["no_windows"],
    deps = [
        "fuzzer_input_cc_proto",
        "fuzzing_common",
        "network_input",
        "//:channel",
        "//:channel_arg_names",
        "//:channel_create",
        "//:config",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc",
        "//:grpc_base",
        "//:grpc_transport_chttp2",
        "//:iomgr",
        "//:orphanable",
        "//:ref_counted_ptr",
        "//src/core:channel_args",
        "//src/core:channel_args_preconditioning",
        "//src/core:channel_stack_type",
        "//src/core:env",
        "//src/core:experiments",
        "//src/core:grpc_check",
        "//test/core/end2end/fuzzers:api_fuzzer_cc_proto",
        "//test/core/event_engine/fuzzing_event_engine",
        "//test/core/test_util:fuzz_config_vars",
        "//test/core/test_util:fuzz_config_vars_helpers",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
    ],
)

grpc_fuzz_test(
    name = "server_fuzzer",
    srcs = ["server_fuzzer.cc"],
    external_deps = [
        "absl/log:check",
        "fuzztest",
        "fuzztest_main",
        "gtest",
    ],
    deps = [
        "fuzzer_input_cc_proto",
        "fuzzing_common",
        "network_input",
        "//:config",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc",
        "//src/core:channel_args",
        "//src/core:chaotic_good_server",
        "//src/core:env",
        "//src/core:event_engine_tcp_socket_utils",
        "//src/core:experiments",
        "//src/core:grpc_check",
        "//src/core:grpc_fake_credentials",
        "//src/core:notification",
        "//test/core/end2end/fuzzers:api_fuzzer_cc_proto",
        "//test/core/event_engine/fuzzing_event_engine",
        "//test/core/test_util:fuzz_config_vars",
        "//test/core/test_util:fuzz_config_vars_helpers",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
        "@com_google_protobuf//:protobuf",
    ],
)

grpc_fuzz_test(
    name = "connector_fuzzer",
    srcs = ["connector_fuzzer.cc"],
    external_deps = [
        "absl/log:check",
        "gtest",
        "fuzztest",
        "fuzztest_main",
    ],
    deps = [
        "fuzzer_input_cc_proto",
        "fuzzing_common",
        "network_input",
        "//:gpr",
        "//:grpc",
        "//:iomgr_timer",
        "//:parse_address",
        "//src/core:channel_args",
        "//src/core:channel_args_endpoint_config",
        "//src/core:env",
        "//src/core:event_engine_tcp_socket_utils",
        "//src/core:grpc_fake_credentials",
        "//src/core:grpc_transport_chttp2_client_connector",
        "//test/core/event_engine/fuzzing_event_engine",
        "//test/core/test_util:fuzz_config_vars",
        "//test/core/test_util:fuzz_config_vars_helpers",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
        "@com_google_protobuf//:protobuf",
    ],
)
