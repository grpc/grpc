# Copyright 2024 gRPC authors.
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

load("//bazel:grpc_build_system.bzl", "grpc_cc_library", "grpc_cc_test", "grpc_package")
load("//test/core/test_util:grpc_fuzzer.bzl", "grpc_fuzz_test")
load("//test/cpp/microbenchmarks:grpc_benchmark_config.bzl", "HISTORY", "MONITORING", "grpc_cc_benchmark")

grpc_package(name = "test/core/call")

GRPC_CALL_SPINE_BENCHMARK_LABELS = {
    "BM_ClientToServerStreaming_CallSpineFixture_:CPU(ns)": {
        "min_threshold": 300,
        "max_threshold": 400,
        "relative_median_threshold": 0.05,
    },
}

grpc_fuzz_test(
    name = "client_call_test",
    srcs = [
        "client_call_test.cc",
    ],
    external_deps = [
        "absl/status",
        "fuzztest",
        "fuzztest_main",
        "gtest",
    ],
    deps = [
        "batch_builder",
        "//:debug_location",
        "//:grpc_base",
        "//src/core:arena",
        "//src/core:metadata",
        "//test/core/call/yodel:yodel_test",
    ],
)

grpc_fuzz_test(
    name = "server_call_test",
    srcs = [
        "server_call_test.cc",
    ],
    external_deps = [
        "absl/status",
        "fuzztest",
        "fuzztest_main",
        "gtest",
    ],
    deps = [
        "batch_builder",
        "//:call_tracer",
        "//:channelz",
        "//:grpc_base",
        "//:promise",
        "//src/core:arena",
        "//test/core/call/yodel:yodel_test",
    ],
)

grpc_cc_test(
    name = "call_utils_test",
    srcs = [
        "call_utils_test.cc",
    ],
    external_deps = ["gtest"],
    deps = [
        "//:grpc_base",
    ],
)

grpc_cc_library(
    name = "batch_builder",
    testonly = True,
    srcs = [
        "batch_builder.cc",
    ],
    hdrs = [
        "batch_builder.h",
    ],
    external_deps = [
        "absl/strings",
        "gtest",
    ],
    visibility = ["//test/core:__subpackages__"],
    deps = [
        "//:grpc",
        "//:grpc_base",
        "//src/core:compression",
        "//src/core:slice",
        "//test/core/end2end:cq_verifier",
    ],
)

grpc_cc_benchmark(
    name = "bm_client_call",
    srcs = ["bm_client_call.cc"],
    monitoring = HISTORY,
    deps = [
        "//:grpc",
        "//:grpc_base",
        "//src/core:all_ok",
        "//src/core:arena",
        "//src/core:call_arena_allocator",
        "//src/core:default_event_engine",
        "//src/core:event_engine_context",
        "//src/core:map",
        "//src/core:resource_quota",
        "//src/core:slice",
    ],
)

grpc_cc_test(
    name = "request_buffer_test",
    srcs = [
        "request_buffer_test.cc",
    ],
    external_deps = ["gtest"],
    deps = [
        "//src/core:request_buffer",
        "//test/core/promise:poll_matcher",
    ],
)

grpc_cc_test(
    name = "filter_fusion_test",
    srcs = [
        "filter_fusion_test.cc",
    ],
    external_deps = [
        "gtest",
        "absl/log",
        "absl/status",
    ],
    deps = [
        "//:grpc_base",
        "//src/core:call_final_info",
        "//src/core:filter_fusion",
    ],
)

grpc_cc_test(
    name = "call_finalization_test",
    srcs = ["call_finalization_test.cc"],
    external_deps = [
        "absl/strings",
        "gtest",
    ],
    deps = [
        "//:event_engine_base_hdrs",
        "//:gpr",
        "//:grpc",
        "//:ref_counted_ptr",
        "//src/core:call_finalization",
        "//src/core:memory_quota",
        "//src/core:resource_quota",
        "//test/core/promise:test_context",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "status_util_test",
    srcs = ["status_util_test.cc"],
    external_deps = [
        "gtest",
    ],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:grpc",
        "//:grpc_base",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "call_arena_allocator_test",
    srcs = ["call_arena_allocator_test.cc"],
    external_deps = [
        "gtest",
        "absl/strings",
    ],
    tags = ["no_windows"],  # TODO(jtattermusch): investigate the timeout on windows
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:exec_ctx",
        "//:gpr",
        "//:grpc",
        "//:ref_counted_ptr",
        "//src/core:call_arena_allocator",
        "//src/core:resource_quota",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "call_filters_test",
    srcs = ["call_filters_test.cc"],
    external_deps = [
        "gtest",
        "@com_google_protobuf//upb/base",
        "@com_google_protobuf//upb/mem",
        "@com_google_protobuf//upb/text",
    ],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:channelz",
        "//:channelz_property_list_upb",
        "//:channelz_property_list_upbdefs",
        "//src/core:call_filters",
        "//src/core:channelz_property_list",
        "//src/core:upb_utils",
        "//src/proto/grpc/channelz/v2:property_list_cc_proto",
        "//test/core/promise:poll_matcher",
    ],
)

grpc_fuzz_test(
    name = "call_spine_test",
    srcs = ["call_spine_test.cc"],
    external_deps = [
        "fuzztest",
        "fuzztest_main",
        "gtest",
        "absl/strings",
    ],
    deps = [
        "//:grpc",
        "//src/core:arena",
        "//src/core:call_spine",
        "//src/core:metadata",
        "//test/core/call/yodel:yodel_test",
    ],
)

grpc_cc_library(
    name = "call_spine_benchmarks",
    testonly = 1,
    hdrs = ["call_spine_benchmarks.h"],
    external_deps = ["benchmark"],
    visibility = ["//test/core:__subpackages__"],
    deps = [
        "//:exec_ctx",
        "//:grpc_base",
        "//src/core:all_ok",
        "//src/core:call_spine",
        "//src/core:default_event_engine",
        "//src/core:event_engine_context",
        "//src/core:map",
        "//src/core:notification",
        "//src/core:resource_quota",
    ],
)

grpc_cc_benchmark(
    name = "bm_call_spine",
    srcs = ["bm_call_spine.cc"],
    labels = GRPC_CALL_SPINE_BENCHMARK_LABELS,
    monitoring = MONITORING,
    deps = [
        ":call_spine_benchmarks",
        "//:gpr",
        "//:grpc",
        "//:grpc_base",
        "//src/core:default_event_engine",
        "//src/core:resource_quota",
    ],
)

grpc_cc_test(
    name = "call_state_test",
    srcs = ["call_state_test.cc"],
    external_deps = [
        "gtest",
    ],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//src/core:call_state",
        "//test/core/promise:poll_matcher",
    ],
)

grpc_cc_test(
    name = "metadata_map_test",
    srcs = ["metadata_map_test.cc"],
    external_deps = [
        "absl/strings",
        "gtest",
    ],
    deps = [
        "//:event_engine_base_hdrs",
        "//:gpr",
        "//:grpc",
        "//:ref_counted_ptr",
        "//src/core:arena",
        "//src/core:memory_quota",
        "//src/core:metadata_batch",
        "//src/core:resource_quota",
        "//src/core:slice",
        "//src/core:time",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
    ],
)

grpc_cc_test(
    name = "parsed_metadata_test",
    srcs = ["parsed_metadata_test.cc"],
    external_deps = [
        "absl/log:check",
        "absl/strings",
        "gtest",
    ],
    deps = [
        "//:gpr",
        "//:grpc",
        "//src/core:grpc_check",
        "//src/core:metadata_batch",
        "//src/core:parsed_metadata",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "interception_chain_test",
    srcs = ["interception_chain_test.cc"],
    external_deps = [
        "absl/log:log",
        "gtest",
    ],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc_base",
        "//src/core:interception_chain",
        "//src/core:resource_quota",
        "//test/core/promise:poll_matcher",
    ],
)
