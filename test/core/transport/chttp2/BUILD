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

grpc_package(name = "test/core/transport/chttp2")

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
        "absl/log:check",
        "gtest",
        "fuzztest",
        "fuzztest_main",
    ],
    deps = [
        "hpack_parser_fuzzer_cc_proto",
        "//:grpc",
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
    ],
    deps = [
        "hpack_sync_fuzzer_cc_proto",
        "//:grpc",
        "//test/core/test_util:fuzz_config_vars",
        "//test/core/test_util:fuzz_config_vars_helpers",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:proto_bit_gen",
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
    ],
    tags = ["no_windows"],
    deps = [
        "flow_control_fuzzer_cc_proto",
        "//:gpr",
        "//src/core:chttp2_flow_control",
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
    ],
    tags = ["no_windows"],
    deps = [
        "//:grpc",
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
        "//:grpc",
        "//src/core:decode_huff",
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
        "//:gpr",
        "//:grpc",
        "//src/core:slice",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "frame_test",
    srcs = ["frame_test.cc"],
    external_deps = ["gtest"],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:chttp2_frame",
        "//src/core:http2_errors",
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
        "//:gpr",
        "//:grpc",
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
        "//:gpr",
        "//:grpc",
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
        "//src/core:chttp2_flow_control",
        "//src/core:resource_quota",
    ],
)

grpc_cc_test(
    name = "graceful_shutdown_test",
    srcs = ["graceful_shutdown_test.cc"],
    external_deps = [
        "absl/log:check",
        "gtest",
    ],
    deps = [
        "//:gpr",
        "//:grpc",
        "//src/core:channel_args",
        "//src/core:closure",
        "//src/core:slice",
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
        "//:gpr",
        "//:grpc",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
    ],
)

grpc_cc_test(
    name = "hpack_parser_test",
    srcs = ["hpack_parser_test.cc"],
    external_deps = ["gtest"],
    tags = ["hpack_test"],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
    ],
)

grpc_cc_test(
    name = "hpack_parser_table_test",
    srcs = ["hpack_parser_table_test.cc"],
    external_deps = ["gtest"],
    tags = ["hpack_test"],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:gpr",
        "//:grpc",
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
    ],
    tags = [
        "requires-net:ipv4",
        "requires-net:loopback",
    ],
    deps = [
        "//:channel_stack_builder",
        "//:gpr",
        "//:grpc",
        "//src/core:channel_args",
        "//src/core:closure",
        "//src/core:slice",
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
    ],
    deps = [
        "//:gpr",
        "//:grpc",
        "//src/core:closure",
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
    ],
    flaky = True,
    shard_count = 7,
    tags = [
        # TODO(yashkt): Re-enable after fixing b/167620746
        "grpc:no-internal-poller",
        "requires-net:ipv4",
        "requires-net:loopback",
    ],
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//src/core:channel_args",
        "//test/core/end2end:cq_verifier",
        "//test/core/test_util:grpc_test_util",
        "//test/cpp/util:test_config",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_test(
    name = "varint_test",
    srcs = ["varint_test.cc"],
    external_deps = [
        "absl/log:log",
        "gtest",
    ],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
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
        "//:gpr",
        "//:grpc",
        "//src/core:channel_args",
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
    ],
    deps = [
        "//:gpr",
        "//:grpc",
        "//src/core:channel_args",
        "//test/core/test_util:grpc_test_util",
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
        "//:gpr",
        "//:grpc",
        "//src/core:http2_settings",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "http2_client_transport_test",
    srcs = ["http2_client_transport_test.cc"],
    external_deps = [
        "absl/log:check",
        "absl/log:log",
        "gtest",
    ],
    tags = ["no_windows"],
    uses_polling = False,
    deps = [
        "//:gpr",
        "//:grpc",
        "//src/core:http2_client_transport",
        "//src/lib/transport:message",
        "//src/lib/transport:metadata",
        "//test/core/promise:poll_matcher",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
        "//test/core/transport/chttp2:http2_frame_test_helper",
        "//test/core/transport/util:mock_promise_endpoint",
        "//test/core/transport/util:transport_test",
    ],
)

grpc_cc_test(
    name = "http2_server_transport_test",
    srcs = ["http2_server_transport_test.cc"],
    external_deps = [
        "absl/log:check",
        "absl/log:log",
        "gtest",
    ],
    tags = ["no_windows"],
    uses_polling = False,
    deps = [
        "//:gpr",
        "//:grpc",
        "//src/core:http2_server_transport",
        "//src/lib/transport:message",
        "//src/lib/transport:metadata",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
        "//test/core/transport/chttp2:http2_frame_test_helper",
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
    external_deps = ["gtest"],
    deps = [
        "//:gpr",
        "//:grpc",
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
        "//src/core:http2_errors",
        "//src/core:slice",
        "//src/core:slice_buffer",
    ],
)
