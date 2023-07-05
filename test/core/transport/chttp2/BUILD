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

load("//bazel:grpc_build_system.bzl", "grpc_cc_test", "grpc_package")
load("//test/core/util:grpc_fuzzer.bzl", "grpc_fuzzer", "grpc_proto_fuzzer")
load("//bazel:custom_exec_properties.bzl", "LARGE_MACHINE")

licenses(["notice"])

grpc_package(name = "test/core/transport/chttp2")

grpc_proto_fuzzer(
    name = "hpack_parser_fuzzer",
    srcs = ["hpack_parser_fuzzer_test.cc"],
    corpus = "hpack_parser_corpus",
    proto = "hpack_parser_fuzzer.proto",
    tags = ["no_windows"],
    uses_polling = False,
    deps = [
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_proto_fuzzer(
    name = "hpack_sync_fuzzer",
    srcs = ["hpack_sync_fuzzer.cc"],
    corpus = "hpack_sync_corpus",
    proto = "hpack_sync_fuzzer.proto",
    tags = ["no_windows"],
    uses_polling = False,
    deps = [
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_proto_fuzzer(
    name = "flow_control_fuzzer",
    srcs = ["flow_control_fuzzer.cc"],
    corpus = "flow_control_fuzzer_corpus",
    language = "C++",
    proto = "flow_control_fuzzer.proto",
    proto_deps = ["//test/core/util:fuzz_config_vars_proto"],
    tags = ["no_windows"],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:gpr",
        "//src/core:chttp2_flow_control",
        "//test/core/util:fuzz_config_vars",
    ],
)

grpc_fuzzer(
    name = "hpack_parser_input_size_fuzzer",
    srcs = ["hpack_parser_input_size_fuzzer.cc"],
    corpus = "hpack_parser_input_size_corpus",
    external_deps = [
        "absl/cleanup",
        "absl/status:statusor",
        "absl/status",
    ],
    tags = ["no_windows"],
    uses_polling = False,
    deps = [
        "//:grpc",
        "//test/core/util:grpc_test_util",
        "//test/core/util:grpc_test_util_base",
    ],
)

grpc_fuzzer(
    name = "decode_huff_fuzzer",
    srcs = ["decode_huff_fuzzer.cc"],
    corpus = "decode_huff_corpus",
    external_deps = [
        "absl/types:optional",
        "absl/strings",
    ],
    language = "C++",
    tags = ["no_windows"],
    deps = [
        "//src/core:decode_huff",
        "//src/core:huffsyms",
    ],
)

grpc_fuzzer(
    name = "encode_decode_huff_fuzzer",
    srcs = ["encode_decode_huff_fuzzer.cc"],
    corpus = "encode_decode_huff_corpus",
    external_deps = [
        "absl/types:optional",
        "absl/strings",
    ],
    language = "C++",
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
    language = "C++",
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "bin_decoder_test",
    srcs = ["bin_decoder_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:gpr",
        "//:grpc",
        "//src/core:slice",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "bin_encoder_test",
    srcs = ["bin_encoder_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:gpr",
        "//:grpc",
        "//src/core:slice",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "flow_control_test",
    srcs = ["flow_control_test.cc"],
    external_deps = [
        "gtest",
    ],
    language = "C++",
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
    external_deps = ["gtest"],
    language = "C++",
    deps = [
        "//:gpr",
        "//:grpc",
        "//src/core:channel_args",
        "//src/core:closure",
        "//src/core:slice",
        "//test/core/end2end:cq_verifier",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "hpack_encoder_test",
    srcs = ["hpack_encoder_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    tags = ["hpack_test"],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:grpc_test_util",
        "//test/core/util:grpc_test_util_base",
    ],
)

grpc_cc_test(
    name = "hpack_parser_test",
    srcs = ["hpack_parser_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    tags = ["hpack_test"],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:grpc_test_util",
        "//test/core/util:grpc_test_util_base",
    ],
)

grpc_cc_test(
    name = "hpack_parser_table_test",
    srcs = ["hpack_parser_table_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    tags = ["hpack_test"],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:grpc_test_util",
        "//test/core/util:grpc_test_util_base",
    ],
)

grpc_cc_test(
    name = "stream_map_test",
    srcs = ["stream_map_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:grpc_test_util",
        "//test/core/util:grpc_test_util_base",
    ],
)

grpc_cc_test(
    name = "streams_not_seen_test",
    srcs = ["streams_not_seen_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    deps = [
        "//:channel_stack_builder",
        "//:gpr",
        "//:grpc",
        "//src/core:channel_args",
        "//src/core:closure",
        "//src/core:slice",
        "//test/core/end2end:cq_verifier",
        "//test/core/util:grpc_test_util",
        "//test/core/util:grpc_test_util_base",
    ],
)

grpc_cc_test(
    name = "settings_timeout_test",
    srcs = ["settings_timeout_test.cc"],
    external_deps = [
        "gtest",
    ],
    language = "C++",
    deps = [
        "//:gpr",
        "//:grpc",
        "//src/core:closure",
        "//test/core/util:grpc_test_util",
        "//test/core/util:grpc_test_util_base",
    ],
)

grpc_cc_test(
    name = "too_many_pings_test",
    timeout = "long",  # Required for internal test infrastructure (cl/325757166)
    srcs = ["too_many_pings_test.cc"],
    external_deps = ["gtest"],
    flaky = True,
    shard_count = 7,
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//src/core:channel_args",
        "//test/core/end2end:cq_verifier",
        "//test/core/util:grpc_test_util",
        "//test/cpp/util:test_config",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_test(
    name = "varint_test",
    srcs = ["varint_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:grpc_test_util",
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
        "gtest",
    ],
    language = "C++",
    tags = [
        "flow_control_test",
        "no_windows",
    ],  # LARGE_MACHINE is not configured for windows RBE
    deps = [
        "//:gpr",
        "//:grpc",
        "//src/core:channel_args",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "stream_leak_with_queued_flow_control_update_test",
    srcs = ["stream_leak_with_queued_flow_control_update_test.cc"],
    external_deps = [
        "gtest",
    ],
    language = "C++",
    deps = [
        "//:gpr",
        "//:grpc",
        "//src/core:channel_args",
        "//test/core/util:grpc_test_util",
    ],
)
