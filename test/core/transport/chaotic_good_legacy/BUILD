# Copyright 2021 gRPC authors.
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

load("//bazel:grpc_build_system.bzl", "grpc_cc_library", "grpc_cc_proto_library", "grpc_cc_test", "grpc_internal_proto_library", "grpc_package")
load("//test/core/test_util:grpc_fuzzer.bzl", "grpc_fuzz_test")

licenses(["notice"])

grpc_package(
    name = "test/core/transport/chaotic_good_legacy",
    visibility = "tests",
)

grpc_cc_library(
    name = "transport_test",
    testonly = 1,
    srcs = ["transport_test.cc"],
    hdrs = ["transport_test.h"],
    external_deps = ["gtest"],
    deps = [
        "//:iomgr_timer",
        "//src/core:chaotic_good_legacy_frame",
        "//src/core:memory_quota",
        "//src/core:resource_quota",
        "//test/core/event_engine/fuzzing_event_engine",
        "//test/core/event_engine/fuzzing_event_engine:fuzzing_event_engine_cc_proto",
    ],
)

grpc_cc_test(
    name = "frame_header_test",
    srcs = ["frame_header_test.cc"],
    external_deps = [
        "absl/status",
        "gtest",
    ],
    deps = ["//src/core:chaotic_good_legacy_frame_header"],
)

grpc_fuzz_test(
    name = "frame_header_fuzzer",
    srcs = ["frame_header_fuzzer.cc"],
    external_deps = [
        "absl/status:statusor",
        "absl/strings",
        "gtest",
        "fuzztest",
        "fuzztest_main",
    ],
    tags = ["no_windows"],
    deps = [
        "//:grpc",
        "//src/core:chaotic_good_legacy_frame_header",
    ],
)

grpc_cc_test(
    name = "frame_test",
    srcs = ["frame_test.cc"],
    external_deps = [
        "absl/log:check",
        "absl/random",
        "absl/status",
        "absl/status:statusor",
        "gtest",
    ],
    deps = [
        "//:channel_create",
        "//src/core:chaotic_good_legacy_frame",
    ],
)

grpc_internal_proto_library(
    name = "frame_fuzzer_proto",
    srcs = ["frame_fuzzer.proto"],
)

grpc_cc_proto_library(
    name = "frame_fuzzer_cc_proto",
    deps = ["frame_fuzzer_proto"],
)

grpc_fuzz_test(
    name = "frame_fuzzer",
    srcs = ["frame_fuzzer.cc"],
    external_deps = [
        "absl/log:check",
        "absl/log:log",
        "absl/random:bit_gen_ref",
        "absl/status:statusor",
        "gtest",
        "fuzztest",
        "fuzztest_main",
    ],
    tags = ["no_windows"],
    deps = [
        "frame_fuzzer_cc_proto",
        "//:exec_ctx",
        "//:gpr",
        "//:hpack_encoder",
        "//:hpack_parser",
        "//:ref_counted_ptr",
        "//src/core:arena",
        "//src/core:chaotic_good_legacy_frame",
        "//src/core:chaotic_good_legacy_frame_header",
        "//src/core:event_engine_memory_allocator",
        "//src/core:memory_quota",
        "//src/core:resource_quota",
        "//src/core:slice",
        "//src/core:slice_buffer",
        "//test/core/promise:test_context",
    ],
)

grpc_cc_test(
    name = "client_transport_test",
    srcs = ["client_transport_test.cc"],
    external_deps = [
        "absl/functional:any_invocable",
        "absl/status:statusor",
        "absl/strings:str_format",
        "gtest",
    ],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "transport_test",
        "//:grpc",
        "//:grpc_public_hdrs",
        "//src/core:arena",
        "//src/core:chaotic_good_legacy_client_transport",
        "//src/core:if",
        "//src/core:loop",
        "//src/core:seq",
        "//src/core:slice_buffer",
        "//test/core/transport/util:mock_promise_endpoint",
    ],
)

grpc_cc_test(
    name = "client_transport_error_test",
    srcs = ["client_transport_error_test.cc"],
    external_deps = [
        "absl/functional:any_invocable",
        "absl/status",
        "absl/status:statusor",
        "absl/strings:str_format",
        "gtest",
    ],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:grpc_public_hdrs",
        "//:grpc_unsecure",
        "//:iomgr_timer",
        "//:ref_counted_ptr",
        "//src/core:activity",
        "//src/core:arena",
        "//src/core:chaotic_good_legacy_client_transport",
        "//src/core:event_engine_wakeup_scheduler",
        "//src/core:grpc_promise_endpoint",
        "//src/core:if",
        "//src/core:join",
        "//src/core:loop",
        "//src/core:memory_quota",
        "//src/core:pipe",
        "//src/core:resource_quota",
        "//src/core:seq",
        "//src/core:slice",
        "//src/core:slice_buffer",
        "//test/core/event_engine/fuzzing_event_engine",
        "//test/core/event_engine/fuzzing_event_engine:fuzzing_event_engine_cc_proto",
    ],
)

grpc_cc_test(
    name = "server_transport_test",
    srcs = ["server_transport_test.cc"],
    external_deps = [
        "absl/functional:any_invocable",
        "absl/status",
        "absl/status:statusor",
        "absl/strings:str_format",
        "gtest",
    ],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "transport_test",
        "//:grpc",
        "//:grpc_public_hdrs",
        "//:iomgr_timer",
        "//:ref_counted_ptr",
        "//src/core:arena",
        "//src/core:chaotic_good_legacy_server_transport",
        "//src/core:memory_quota",
        "//src/core:resource_quota",
        "//src/core:seq",
        "//src/core:slice",
        "//src/core:slice_buffer",
        "//test/core/event_engine/fuzzing_event_engine",
        "//test/core/event_engine/fuzzing_event_engine:fuzzing_event_engine_cc_proto",
        "//test/core/transport/util:mock_promise_endpoint",
    ],
)

grpc_fuzz_test(
    name = "control_endpoint_test",
    srcs = ["control_endpoint_test.cc"],
    external_deps = [
        "fuzztest",
        "fuzztest_main",
        "gtest",
    ],
    tags = [
        "no_windows",
    ],
    deps = [
        "//src/core:chaotic_good_legacy_control_endpoint",
        "//test/core/call/yodel:yodel_test",
        "//test/core/transport/util:mock_promise_endpoint",
    ],
)

grpc_fuzz_test(
    name = "data_endpoints_test",
    srcs = ["data_endpoints_test.cc"],
    external_deps = [
        "fuzztest",
        "fuzztest_main",
        "gtest",
    ],
    tags = [
        "no_windows",
    ],
    deps = [
        "//src/core:chaotic_good_legacy_data_endpoints",
        "//test/core/call/yodel:yodel_test",
        "//test/core/transport/util:mock_promise_endpoint",
    ],
)

grpc_fuzz_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    external_deps = [
        "fuzztest",
        "fuzztest_main",
        "gtest",
    ],
    deps = [
        "//src/core:chaotic_good_frame_cc_proto",
        "//src/core:chaotic_good_legacy_config",
    ],
)

grpc_fuzz_test(
    name = "message_chunker_test",
    srcs = ["message_chunker_test.cc"],
    external_deps = [
        "fuzztest",
        "fuzztest_main",
        "gtest",
    ],
    deps = [
        "//src/core:chaotic_good_frame_cc_proto",
        "//src/core:chaotic_good_legacy_message_chunker",
        "//src/core:status_flag",
        "//test/core/promise:poll_matcher",
    ],
)
