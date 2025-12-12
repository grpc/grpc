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

load("//bazel:custom_exec_properties.bzl", "LARGE_MACHINE")
load("//bazel:grpc_build_system.bzl", "grpc_cc_library", "grpc_cc_proto_library", "grpc_cc_test", "grpc_internal_proto_library", "grpc_package")
load("//test/core/test_util:grpc_fuzzer.bzl", "grpc_fuzz_test")

licenses(["notice"])

grpc_package(
    name = "test/core/transport/chttp2",
    features = [
        "layering_check",
        "parse_headers",
    ],
)

grpc_internal_proto_library(
    name = "hpack_parser_fuzzer_proto",
    srcs = ["hpack_parser_fuzzer.proto"],
    deps = ["//test/core/test_util:fuzz_config_vars_proto"],
)

grpc_cc_proto_library(
    name = "hpack_parser_fuzzer_cc_proto",
    deps = ["hpack_parser_fuzzer_proto"],
)

grpc_fuzz_test(
    name = "hpack_parser_fuzzer",
    srcs = ["hpack_parser_fuzzer.cc"],
    external_deps = [
        "absl/cleanup",
        "absl/log:check",
        "absl/random:bit_gen_ref",
        "gtest",
        "fuzztest",
        "fuzztest_main",
    ],
    deps = [
        "hpack_parser_fuzzer_cc_proto",
        "//:exec_ctx",
        "//:grpc",
        "//:hpack_parser",
        "//:ref_counted_ptr",
        "//src/core:arena",
        "//src/core:error",
        "//src/core:experiments",
        "//src/core:grpc_check",
        "//src/core:memory_quota",
        "//src/core:metadata_batch",
        "//src/core:resource_quota",
        "//src/core:status_helper",
        "//test/core/test_util:fuzz_config_vars",
        "//test/core/test_util:fuzz_config_vars_helpers",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:proto_bit_gen",
    ],
)

grpc_internal_proto_library(
    name = "hpack_sync_fuzzer_proto",
    srcs = ["hpack_sync_fuzzer.proto"],
    deps = ["//test/core/test_util:fuzz_config_vars_proto"],
)

grpc_cc_proto_library(
    name = "hpack_sync_fuzzer_cc_proto",
    deps = ["hpack_sync_fuzzer_proto"],
)

grpc_fuzz_test(
    name = "hpack_sync_fuzzer",
    srcs = ["hpack_sync_fuzzer.cc"],
    external_deps = [
        "absl/log:check",
        "gtest",
        "fuzztest",
        "fuzztest_main",
        "protobuf",
        "absl/random:bit_gen_ref",
        "absl/status",
        "absl/strings",
    ],
    deps = [
        "hpack_sync_fuzzer_cc_proto",
        "//:exec_ctx",
        "//:grpc",
        "//:hpack_encoder",
        "//:hpack_parser",
        "//:hpack_parser_table",
        "//:ref_counted_ptr",
        "//src/core:arena",
        "//src/core:error",
        "//src/core:experiments",
        "//src/core:hpack_encoder_table",
        "//src/core:memory_quota",
        "//src/core:metadata_batch",
        "//src/core:resource_quota",
        "//src/core:slice",
        "//src/core:slice_buffer",
        "//src/core:status_helper",
        "//test/core/test_util:fuzz_config_vars",
        "//test/core/test_util:fuzz_config_vars_helpers",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:proto_bit_gen",
        "@com_google_protobuf//:protobuf",
    ],
)

grpc_internal_proto_library(
    name = "flow_control_fuzzer_proto",
    srcs = ["flow_control_fuzzer.proto"],
    deps = ["//test/core/test_util:fuzz_config_vars_proto"],
)

grpc_cc_proto_library(
    name = "flow_control_fuzzer_cc_proto",
    deps = ["flow_control_fuzzer_proto"],
)

grpc_fuzz_test(
    name = "flow_control_fuzzer",
    srcs = ["flow_control_fuzzer.cc"],
    external_deps = [
        "absl/log:check",
        "fuzztest",
        "fuzztest_main",
        "absl/base",
        "absl/base:core_headers",
        "absl/status",
        "absl/strings",
    ],
    tags = ["no_windows"],
    deps = [
        "flow_control_fuzzer_cc_proto",
        "//:exec_ctx",
        "//:gpr",
        "//src/core:bdp_estimator",
        "//src/core:chttp2_flow_control",
        "//src/core:event_engine_memory_allocator",
        "//src/core:experiments",
        "//src/core:grpc_check",
        "//src/core:memory_quota",
        "//src/core:time",
        "//src/core:useful",
        "//test/core/test_util:fuzz_config_vars",
        "//test/core/test_util:fuzz_config_vars_helpers",
    ],
)

grpc_fuzz_test(
    name = "hpack_parser_input_size_fuzzer",
    srcs = ["hpack_parser_input_size_fuzzer.cc"],
    external_deps = [
        "absl/cleanup",
        "absl/status:statusor",
        "absl/status",
        "gtest",
        "fuzztest",
        "fuzztest_main",
        "absl/random:bit_gen_ref",
        "absl/strings",
    ],
    tags = ["no_windows"],
    deps = [
        "//:exec_ctx",
        "//:gpr",
        "//:grpc",
        "//:hpack_parser",
        "//:ref_counted_ptr",
        "//src/core:arena",
        "//src/core:error",
        "//src/core:event_engine_memory_allocator",
        "//src/core:memory_quota",
        "//src/core:metadata_batch",
        "//src/core:resource_quota",
        "//src/core:slice",
        "//src/core:status_helper",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
    ],
)

grpc_fuzz_test(
    name = "huff_fuzzer",
    srcs = ["huff_fuzzer.cc"],
    external_deps = [
        "absl/strings",
        "gtest",
        "fuzztest",
        "fuzztest_main",
    ],
    tags = ["no_windows"],
    deps = [
        "//:chttp2_bin_encoder",
        "//:grpc",
        "//src/core:decode_huff",
        "//src/core:dump_args",
        "//src/core:huffsyms",
    ],
)

grpc_cc_test(
    name = "alpn_test",
    srcs = ["alpn_test.cc"],
    external_deps = ["gtest"],
    deps = [
        "//:gpr",
        "//:grpc",
        "//src/core:grpc_transport_chttp2_alpn",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "bin_decoder_test",
    srcs = ["bin_decoder_test.cc"],
    external_deps = [
        "absl/log:log",
        "gtest",
    ],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:chttp2_bin_encoder",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc",
        "//:grpc_transport_chttp2",
        "//src/core:slice",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "frame_test",
    srcs = ["frame_test.cc"],
    external_deps = [
        "gtest",
        "absl/status",
    ],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:chttp2_frame",
        "//src/core:http2_settings",
        "//src/core:http2_status",
        "//src/core:message",
        "//src/core:slice_buffer",
    ],
)

grpc_cc_test(
    name = "bin_encoder_test",
    srcs = ["bin_encoder_test.cc"],
    external_deps = [
        "absl/log:log",
        "gtest",
    ],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:chttp2_bin_encoder",
        "//:gpr",
        "//:grpc",
        "//src/core:slice",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "ping_abuse_policy_test",
    srcs = ["ping_abuse_policy_test.cc"],
    external_deps = ["gtest"],
    uses_polling = False,
    deps = [
        "//:channel_arg_names",
        "//:gpr",
        "//:grpc",
        "//src/core:ping_abuse_policy",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
    ],
)

grpc_cc_test(
    name = "ping_rate_policy_test",
    srcs = ["ping_rate_policy_test.cc"],
    external_deps = ["gtest"],
    uses_polling = False,
    deps = [
        "//:gpr",
        "//:grpc",
        "//src/core:experiments",
        "//src/core:ping_rate_policy",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
    ],
)

grpc_cc_test(
    name = "ping_configuration_test",
    srcs = ["ping_configuration_test.cc"],
    external_deps = ["gtest"],
    tags = [
        # HTTP2 pings configuration is different internally.
        "grpc:no-internal-poller",
    ],
    uses_polling = False,
    deps = [
        "//:channel_arg_names",
        "//:chttp2_legacy_frame",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc",
        "//:grpc_transport_chttp2",
        "//:iomgr",
        "//src/core:channel_args",
        "//src/core:default_event_engine",
        "//src/core:experiments",
        "//src/core:ping_abuse_policy",
        "//src/core:ping_rate_policy",
        "//src/core:resource_quota",
        "//src/core:time",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
    ],
)

grpc_cc_test(
    name = "ping_callbacks_test",
    srcs = ["ping_callbacks_test.cc"],
    external_deps = [
        "gtest",
        "absl/random",
    ],
    uses_polling = False,
    deps = [
        "//:gpr",
        "//:grpc",
        "//src/core:ping_callbacks",
        "//test/core/event_engine:mock_event_engine",
    ],
)

grpc_cc_test(
    name = "write_size_policy_test",
    srcs = ["write_size_policy_test.cc"],
    external_deps = [
        "gtest",
        "absl/random",
    ],
    uses_polling = False,
    deps = [
        "//src/core:write_size_policy",
    ],
)

grpc_cc_test(
    name = "flow_control_test",
    srcs = ["flow_control_test.cc"],
    external_deps = [
        "absl/log:check",
        "gtest",
    ],
    tags = ["flow_control_test"],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:exec_ctx",
        "//:gpr",
        "//:ref_counted_ptr",
        "//src/core:bdp_estimator",
        "//src/core:chttp2_flow_control",
        "//src/core:experiments",
        "//src/core:grpc_check",
        "//src/core:resource_quota",
        "//src/core:time",
        "//src/core:useful",
    ],
)

grpc_cc_test(
    name = "flow_control_manager_test",
    srcs = ["flow_control_manager_test.cc"],
    external_deps = [
        "gtest",
    ],
    deps = [
        "//:chttp2_frame",
        "//src/core:chttp2_flow_control",
        "//src/core:flow_control_manager",
        "//src/core:http2_settings",
    ],
)

grpc_cc_test(
    name = "graceful_shutdown_test",
    srcs = ["graceful_shutdown_test.cc"],
    external_deps = [
        "absl/log:check",
        "gtest",
        "absl/base:base",
        "absl/status",
        "absl/strings",
        "absl/time",
        "absl/base:core_headers",
    ],
    deps = [
        "//:channel_arg_names",
        "//:channelz",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc",
        "//:grpc_base",
        "//:grpc_transport_chttp2",
        "//:iomgr",
        "//:orphanable",
        "//:server",
        "//src/core:channel_args",
        "//src/core:closure",
        "//src/core:error",
        "//src/core:grpc_check",
        "//src/core:internal_channel_arg_names",
        "//src/core:notification",
        "//src/core:slice",
        "//src/core:sync",
        "//src/core:useful",
        "//test/core/end2end:cq_verifier",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "hpack_encoder_test",
    srcs = ["hpack_encoder_test.cc"],
    external_deps = [
        "absl/log:log",
        "gtest",
    ],
    tags = ["hpack_test"],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:chttp2_legacy_frame",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc",
        "//:hpack_encoder",
        "//:ref_counted_ptr",
        "//src/core:arena",
        "//src/core:event_engine_memory_allocator",
        "//src/core:grpc_check",
        "//src/core:memory_quota",
        "//src/core:resource_quota",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
    ],
)

grpc_cc_test(
    name = "hpack_parser_test",
    srcs = ["hpack_parser_test.cc"],
    external_deps = [
        "absl/cleanup",
        "absl/random",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "gtest",
    ],
    tags = ["hpack_test"],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:exec_ctx",
        "//:gpr",
        "//:grpc",
        "//:hpack_parser",
        "//:ref_counted_ptr",
        "//src/core:arena",
        "//src/core:error_utils",
        "//src/core:event_engine_memory_allocator",
        "//src/core:memory_quota",
        "//src/core:resource_quota",
        "//src/core:slice",
        "//src/core:status_helper",
        "//src/core:time",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
    ],
)

grpc_cc_test(
    name = "hpack_parser_table_test",
    srcs = ["hpack_parser_table_test.cc"],
    external_deps = [
        "gtest",
        "absl/strings",
    ],
    tags = ["hpack_test"],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:exec_ctx",
        "//:gpr",
        "//:grpc",
        "//:hpack_parser_table",
        "//:stats",
        "//src/core:slice",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
    ],
)

grpc_cc_test(
    name = "streams_not_seen_test",
    srcs = ["streams_not_seen_test.cc"],
    external_deps = [
        "absl/log:check",
        "gtest",
        "absl/base:core_headers",
        "absl/status",
        "absl/strings",
        "absl/time",
    ],
    tags = [
        "requires-net:ipv4",
        "requires-net:loopback",
    ],
    deps = [
        "//:channel_arg_names",
        "//:channel_stack_builder",
        "//:config",
        "//:debug_location",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc",
        "//:grpc_base",
        "//:grpc_core_credentials_header",
        "//:grpc_credentials_util",
        "//:grpc_transport_chttp2",
        "//:iomgr",
        "//src/core:channel_args",
        "//src/core:channel_fwd",
        "//src/core:channel_stack_type",
        "//src/core:closure",
        "//src/core:error",
        "//src/core:grpc_check",
        "//src/core:iomgr_fwd",
        "//src/core:metadata_batch",
        "//src/core:notification",
        "//src/core:slice",
        "//src/core:sync",
        "//src/core:time",
        "//src/core:useful",
        "//test/core/end2end:cq_verifier",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
    ],
)

grpc_cc_test(
    name = "settings_timeout_test",
    srcs = ["settings_timeout_test.cc"],
    external_deps = [
        "absl/log:check",
        "absl/log:log",
        "gtest",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
    ],
    deps = [
        "//:channel_arg_names",
        "//:config",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc",
        "//:grpc_core_credentials_header",
        "//:iomgr",
        "//:resource_quota_api",
        "//src/core:channel_args_endpoint_config",
        "//src/core:channel_args_preconditioning",
        "//src/core:closure",
        "//src/core:error",
        "//src/core:event_engine_common",
        "//src/core:event_engine_shim",
        "//src/core:event_engine_utils",
        "//src/core:grpc_check",
        "//src/core:iomgr_fwd",
        "//src/core:pollset_set",
        "//src/core:resolved_address",
        "//src/core:status_helper",
        "//src/core:time",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
    ],
)

grpc_cc_test(
    name = "too_many_pings_test",
    timeout = "long",  # Required for internal test infrastructure (cl/325757166)
    srcs = ["too_many_pings_test.cc"],
    external_deps = [
        "absl/log:check",
        "absl/log:log",
        "gtest",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/time",
    ],
    flaky = True,
    shard_count = 7,
    tags = [
        # TODO(yashkt): Re-enable after fixing b/167620746
        "grpc:no-internal-poller",
        "requires-net:ipv4",
        "requires-net:loopback",
        "chttp2_keepalive_tests",
    ],
    deps = [
        "//:channel",
        "//:channel_arg_names",
        "//:endpoint_addresses",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//:grpc_core_credentials_header",
        "//:grpc_resolver",
        "//:grpc_resolver_fake",
        "//:grpc_transport_chttp2",
        "//:parse_address",
        "//:ref_counted_ptr",
        "//:uri",
        "//src/core:channel_args",
        "//src/core:grpc_check",
        "//src/core:resolved_address",
        "//src/core:slice",
        "//src/core:sync",
        "//src/core:time",
        "//src/core:useful",
        "//test/core/end2end:cq_verifier",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
        "//test/cpp/util:test_config",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_test(
    name = "varint_test",
    srcs = ["varint_test.cc"],
    external_deps = [
        "absl/log:log",
        "absl/strings:str_format",
        "gtest",
    ],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:chttp2_varint",
        "//:gpr",
        "//:grpc",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "remove_stream_from_stalled_lists_test",
    srcs = ["remove_stream_from_stalled_lists_test.cc"],
    # use LARGE_MACHINE because this test needs a certaim amount
    # of parallelism in order to reproduce the original crash that it's meant
    # to regression test for (a crash which is fixed by
    # https://github.com/grpc/grpc/pull/23984). Experiments show that if this
    # test doesn't use LARGE_MACHINE, then it will almost never reproduce the
    # intended crash (as it's meant to before the fix is applied). But with
    # LARGE_MACHINE, it can reproduce at a high rate.
    exec_properties = LARGE_MACHINE,
    external_deps = [
        "absl/log:check",
        "absl/log:log",
        "gtest",
    ],
    tags = [
        "flow_control_test",
        "no_windows",
    ],  # LARGE_MACHINE is not configured for windows RBE
    deps = [
        "//:channel_arg_names",
        "//:config_vars",
        "//:gpr",
        "//:grpc",
        "//:grpc_core_credentials_header",
        "//:grpc_public_hdrs",
        "//src/core:channel_args",
        "//src/core:chttp2_flow_control",
        "//src/core:grpc_check",
        "//src/core:sync",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "stream_leak_with_queued_flow_control_update_test",
    srcs = ["stream_leak_with_queued_flow_control_update_test.cc"],
    external_deps = [
        "absl/log:check",
        "absl/log:log",
        "gtest",
        "absl/base:core_headers",
        "absl/strings",
    ],
    deps = [
        "//:channel_arg_names",
        "//:gpr",
        "//:grpc",
        "//:grpc_core_credentials_header",
        "//:grpc_public_hdrs",
        "//:grpc_transport_chttp2",
        "//src/core:channel_args",
        "//src/core:grpc_check",
        "//src/core:sync",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_library(
    name = "http2_common_test_inputs",
    hdrs = [
        "http2_common_test_inputs.h",
    ],
    external_deps = [
        "absl/log:check",
        "absl/log:log",
        "absl/strings",
    ],
    deps = [
        "//:chttp2_frame",
        "//:gpr_platform",
        "//src/core:http2_status",
    ],
)

grpc_cc_test(
    name = "http2_settings_test",
    srcs = [
        "http2_settings_test.cc",
    ],
    external_deps = [
        "gtest",
    ],
    deps = [
        "//:chttp2_frame",
        "//:gpr",
        "//:grpc",
        "//src/core:http2_settings",
        "//src/core:http2_settings_manager",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "incoming_metadata_tracker_test",
    srcs = [
        "incoming_metadata_tracker_test.cc",
    ],
    external_deps = [
        "gtest",
        "absl/log:log",
    ],
    deps = [
        ":http2_common_test_inputs",
        "//:chttp2_frame",
        "//:grpc_base",
        "//src/core:grpc_check",
        "//src/core:http2_status",
        "//src/core:incoming_metadata_tracker",
    ],
)

grpc_cc_test(
    name = "header_assembler_test",
    srcs = [
        "header_assembler_test.cc",
    ],
    external_deps = [
        "absl/log:check",
        "absl/log:log",
        "absl/status",
        "gtest",
        "absl/random",
        "absl/strings",
    ],
    deps = [
        ":http2_common_test_inputs",
        "//:chttp2_frame",
        "//:grpc",
        "//:grpc_base",
        "//:hpack_encoder",
        "//:hpack_parser",
        "//:ref_counted_ptr",
        "//src/core:arena",
        "//src/core:grpc_check",
        "//src/core:header_assembler",
        "//src/core:http2_settings",
        "//src/core:http2_status",
        "//src/core:message",
        "//src/core:metadata_batch",
        "//src/core:slice",
        "//src/core:slice_buffer",
    ],
)

grpc_cc_test(
    name = "message_assembler_test",
    srcs = [
        "message_assembler_test.cc",
    ],
    external_deps = [
        "absl/log:check",
        "absl/log:log",
        "absl/strings",
        "gtest",
    ],
    deps = [
        ":http2_common_test_inputs",
        "//:chttp2_frame",
        "//:grpc",
        "//:grpc_base",
        "//:ref_counted_ptr",
        "//src/core:arena",
        "//src/core:grpc_check",
        "//src/core:message",
        "//src/core:message_assembler",
        "//src/core:slice",
        "//src/core:slice_buffer",
    ],
)

grpc_fuzz_test(
    name = "message_assembler_fuzz_test",
    srcs = ["message_assembler_fuzz_test.cc"],
    external_deps = [
        "absl/log:check",
        "absl/log:log",
        "fuzztest",
        "fuzztest_main",
        "gtest",
        "absl/strings",
    ],
    deps = [
        ":http2_common_test_inputs",
        "//:chttp2_frame",
        "//:grpc",
        "//:ref_counted_ptr",
        "//src/core:grpc_check",
        "//src/core:message",
        "//src/core:message_assembler",
        "//src/core:slice",
        "//src/core:slice_buffer",
    ],
)

grpc_cc_test(
    name = "http2_client_transport_test",
    srcs = ["http2_client_transport_test.cc"],
    external_deps = [
        "absl/functional:any_invocable",
        "absl/log:check",
        "absl/log:log",
        "absl/status",
        "absl/strings",
        "gtest",
    ],
    uses_polling = False,
    deps = [
        ":http2_frame_test_helper",
        "//:channel_arg_names",
        "//:chttp2_frame",
        "//:config",
        "//:event_engine_base_hdrs",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc",
        "//:grpc_transport_chttp2",
        "//:orphanable",
        "//src/core:arena",
        "//src/core:call_spine",
        "//src/core:channel_args",
        "//src/core:chttp2_flow_control",
        "//src/core:default_event_engine",
        "//src/core:http2_client_transport",
        "//src/core:http2_settings",
        "//src/core:http2_settings_manager",
        "//src/core:http2_status",
        "//src/core:http2_transport",
        "//src/core:map",
        "//src/core:message",
        "//src/core:metadata",
        "//src/core:metadata_batch",
        "//src/core:notification",
        "//src/core:poll",
        "//src/core:seq",
        "//src/core:sleep",
        "//src/core:slice",
        "//src/core:slice_buffer",
        "//src/core:time",
        "//src/core:transport_common",
        "//src/core:try_join",
        "//src/core:try_seq",
        "//test/core/promise:poll_matcher",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
        "//test/core/test_util:postmortem",
        "//test/core/transport/util:mock_promise_endpoint",
        "//test/core/transport/util:transport_test",
    ],
)

grpc_cc_test(
    name = "settings_timeout_manager_test",
    srcs = ["settings_timeout_manager_test.cc"],
    external_deps = [
        "absl/log:check",
        "absl/log:log",
        "absl/status",
        "absl/strings",
        "absl/types:span",
        "gtest",
    ],
    uses_polling = False,
    deps = [
        ":http2_frame_test_helper",
        "//:chttp2_frame",
        "//:config",
        "//:event_engine_base_hdrs",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc",
        "//:orphanable",
        "//:ref_counted_ptr",
        "//src/core:1999",
        "//src/core:arena",
        "//src/core:call_spine",
        "//src/core:channel_args",
        "//src/core:chttp2_flow_control",
        "//src/core:default_event_engine",
        "//src/core:http2_client_transport",
        "//src/core:http2_settings",
        "//src/core:http2_settings_manager",
        "//src/core:http2_settings_promises",
        "//src/core:http2_status",
        "//src/core:http2_transport",
        "//src/core:map",
        "//src/core:message",
        "//src/core:metadata",
        "//src/core:notification",
        "//src/core:poll",
        "//src/core:sleep",
        "//src/core:slice",
        "//src/core:slice_buffer",
        "//src/core:time",
        "//src/core:transport_common",
        "//src/core:try_join",
        "//src/core:try_seq",
        "//test/core/promise:poll_matcher",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
        "//test/core/test_util:postmortem",
        "//test/core/transport/util:mock_promise_endpoint",
        "//test/core/transport/util:transport_test",
    ],
)

grpc_cc_test(
    name = "http2_transport_test",
    srcs = ["http2_transport_test.cc"],
    external_deps = [
        "absl/log:check",
        "absl/log:log",
        "absl/status",
        "absl/strings",
        "absl/types:span",
        "gtest",
    ],
    uses_polling = False,
    deps = [
        ":http2_frame_test_helper",
        "//:chttp2_frame",
        "//:config",
        "//:event_engine_base_hdrs",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc",
        "//:grpc_base",
        "//:orphanable",
        "//:promise",
        "//:ref_counted_ptr",
        "//src/core:1999",
        "//src/core:call_spine",
        "//src/core:channel_args",
        "//src/core:chttp2_flow_control",
        "//src/core:default_event_engine",
        "//src/core:grpc_check",
        "//src/core:http2_client_transport",
        "//src/core:http2_settings",
        "//src/core:http2_settings_manager",
        "//src/core:http2_settings_promises",
        "//src/core:http2_status",
        "//src/core:http2_transport",
        "//src/core:internal_channel_arg_names",
        "//src/core:loop",
        "//src/core:message",
        "//src/core:metadata",
        "//src/core:notification",
        "//src/core:ph2_stream",
        "//src/core:poll",
        "//src/core:ref_counted",
        "//src/core:sleep",
        "//src/core:slice_buffer",
        "//src/core:time",
        "//src/core:transport_common",
        "//src/core:try_join",
        "//src/core:try_seq",
        "//test/core/promise:poll_matcher",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
        "//test/core/test_util:postmortem",
        "//test/core/transport/util:mock_promise_endpoint",
        "//test/core/transport/util:transport_test",
    ],
)

grpc_cc_test(
    name = "http2_status_test",
    srcs = [
        "http2_status_test.cc",
    ],
    external_deps = [
        "absl/log:check",
        "absl/log:log",
        "absl/status",
        "gtest",
    ],
    deps = [
        "//:chttp2_frame",
        "//src/core:grpc_check",
        "//src/core:http2_status",
        "//test/core/transport/chttp2:http2_common_test_inputs",
    ],
)

grpc_cc_test(
    name = "http2_server_transport_test",
    srcs = ["http2_server_transport_test.cc"],
    external_deps = [
        "absl/log:check",
        "absl/log:log",
        "gtest",
        "absl/status",
        "absl/strings",
    ],
    tags = ["no_windows"],
    uses_polling = False,
    deps = [
        ":http2_frame_test_helper",
        "//:event_engine_base_hdrs",
        "//:gpr",
        "//:grpc",
        "//:orphanable",
        "//src/core:http2_server_transport",
        "//src/core:message",
        "//src/core:metadata",
        "//src/core:notification",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
        "//test/core/transport/util:mock_promise_endpoint",
        "//test/core/transport/util:transport_test",
    ],
)

grpc_cc_test(
    name = "chttp2_server_listener_test",
    srcs = ["chttp2_server_listener_test.cc"],
    data = [
        "//src/core/tsi/test_creds:client.pem",
        "//src/core/tsi/test_creds:server1.key",
        "//src/core/tsi/test_creds:server1.pem",
    ],
    external_deps = [
        "gtest",
        "absl/synchronization",
    ],
    deps = [
        "//:config",
        "//:gpr",
        "//:grpc",
        "//:grpc_security_base",
        "//:server",
        "//src/core:grpc_insecure_credentials",
        "//src/core:grpc_tls_credentials",
        "//src/core:grpc_transport_chttp2_server",
        "//test/core/end2end:cq_verifier",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
    ],
)

grpc_fuzz_test(
    name = "write_size_policy_fuzztest",
    srcs = ["write_size_policy_fuzztest.cc"],
    external_deps = [
        "fuzztest",
        "fuzztest_main",
        "gtest",
    ],
    deps = ["//src/core:write_size_policy"],
)

grpc_fuzz_test(
    name = "hpack_encoder_timeout_test",
    srcs = ["hpack_encoder_timeout_test.cc"],
    external_deps = [
        "absl/random",
        "fuzztest",
        "fuzztest_main",
        "gtest",
    ],
    deps = [
        "//:grpc_base",
        "//:hpack_encoder",
        "//:hpack_parser",
        "//:ref_counted_ptr",
        "//src/core:arena",
        "//src/core:memory_quota",
        "//src/core:metadata_batch",
        "//src/core:resource_quota",
        "//src/core:slice_buffer",
        "//src/core:time",
    ],
)

grpc_cc_library(
    name = "http2_frame_test_helper",
    testonly = 1,
    hdrs = ["http2_frame_test_helper.h"],
    external_deps = [
        "absl/log:log",
        "absl/strings",
        "absl/types:span",
    ],
    deps = [
        "//:chttp2_frame",
        "//src/core:default_event_engine",
        "//src/core:grpc_promise_endpoint",
        "//src/core:http2_settings",
        "//src/core:http2_settings_manager",
        "//src/core:http2_status",
        "//src/core:slice",
        "//src/core:slice_buffer",
    ],
)

grpc_fuzz_test(
    name = "ping_promise_test",
    srcs = [
        "ping_promise_test.cc",
    ],
    external_deps = [
        "fuzztest",
        "fuzztest_main",
        "gtest",
        "absl/log",
    ],
    deps = [
        "//:config",
        "//:ref_counted_ptr",
        "//src/core:1999",
        "//src/core:map",
        "//src/core:notification",
        "//src/core:ping_promise",
        "//src/core:sleep",
        "//src/core:time",
        "//src/core:try_seq",
        "//test/core/call/yodel:yodel_test",
    ],
)

grpc_fuzz_test(
    name = "keepalive_test",
    srcs = [
        "keepalive_test.cc",
    ],
    external_deps = [
        "fuzztest",
        "fuzztest_main",
        "gtest",
        "absl/log:log",
        "absl/status",
    ],
    deps = [
        "//:promise",
        "//src/core:keepalive",
        "//src/core:loop",
        "//src/core:map",
        "//src/core:sleep",
        "//src/core:time",
        "//src/core:try_seq",
        "//test/core/call/yodel:yodel_test",
    ],
)

grpc_cc_test(
    name = "stream_data_queue_test",
    srcs = ["stream_data_queue_test.cc"],
    external_deps = [
        "gtest",
        "absl/log:log",
        "absl/status",
    ],
    deps = [
        ":http2_common_test_inputs",
        "//:chttp2_frame",
        "//:debug_location",
        "//:event_engine_base_hdrs",
        "//:grpc",
        "//:hpack_encoder",
        "//:ref_counted_ptr",
        "//src/core:1999",
        "//src/core:arena",
        "//src/core:if",
        "//src/core:latch",
        "//src/core:loop",
        "//src/core:map",
        "//src/core:message",
        "//src/core:metadata",
        "//src/core:metadata_batch",
        "//src/core:sleep",
        "//src/core:slice",
        "//src/core:slice_buffer",
        "//src/core:status_flag",
        "//src/core:stream_data_queue",
        "//src/core:time",
        "//src/core:transport_common",
        "//src/core:try_seq",
        "//test/core/transport/util:transport_test",
    ],
)

grpc_fuzz_test(
    name = "stream_data_queue_fuzz_test",
    srcs = ["stream_data_queue_fuzz_test.cc"],
    external_deps = [
        "fuzztest",
        "fuzztest_main",
        "gtest",
        "absl/log:log",
        "absl/status",
        "absl/strings",
    ],
    deps = [
        "//:chttp2_frame",
        "//:event_engine_base_hdrs",
        "//:gpr",
        "//:hpack_encoder",
        "//:hpack_parser",
        "//:ref_counted_ptr",
        "//src/core:1999",
        "//src/core:arena",
        "//src/core:grpc_check",
        "//src/core:header_assembler",
        "//src/core:http2_settings",
        "//src/core:http2_status",
        "//src/core:if",
        "//src/core:loop",
        "//src/core:map",
        "//src/core:message",
        "//src/core:message_assembler",
        "//src/core:metadata",
        "//src/core:metadata_batch",
        "//src/core:sleep",
        "//src/core:slice",
        "//src/core:slice_buffer",
        "//src/core:status_flag",
        "//src/core:stream_data_queue",
        "//src/core:time",
        "//src/core:try_seq",
        "//test/core/call/yodel:yodel_test",
    ],
)

grpc_cc_test(
    name = "writable_streams_test",
    srcs = ["writable_streams_test.cc"],
    external_deps = [
        "gtest",
    ],
    deps = [
        "//src/core:join",
        "//src/core:loop",
        "//src/core:writable_streams",
        "//test/core/transport/util:transport_test",
    ],
)

grpc_fuzz_test(
    name = "writable_streams_fuzz_test",
    srcs = ["writable_streams_fuzz_test.cc"],
    external_deps = [
        "fuzztest",
        "fuzztest_main",
        "gtest",
    ],
    deps = [
        "//src/core:loop",
        "//src/core:sleep",
        "//src/core:writable_streams",
        "//test/core/call/yodel:yodel_test",
    ],
)

grpc_fuzz_test(
    name = "goaway_fuzz_test",
    srcs = ["goaway_test.cc"],
    external_deps = [
        "absl/strings:string_view",
        "absl/status",
        "fuzztest",
        "fuzztest_main",
        "gtest",
    ],
    deps = [
        "//:config",
        "//src/core:goaway",
        "//src/core:http2_status",
        "//src/core:loop",
        "//test/core/call/yodel:yodel_test",
    ],
)
