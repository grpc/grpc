# Copyright 2022 gRPC authors.
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
load("//test/cpp/microbenchmarks:grpc_benchmark_config.bzl", "HISTORY", "grpc_cc_benchmark")

licenses(["notice"])

grpc_package(
    name = "test/core/filters",
    features = [
        "layering_check",
        "parse_headers",
    ],
)

# gmock matchers for metadata/messages flowing through a filter call. Depend on
# this directly from tests that use the matchers; it is independent of which
# harness (FilterTest or FilterTestV2) the test is written against.
grpc_cc_library(
    name = "filter_matchers",
    testonly = 1,
    hdrs = ["filter_matchers.h"],
    external_deps = [
        "absl/strings",
        "gtest",
    ],
    visibility = ["//test:__subpackages__"],
    deps = [
        "//src/core:message",
        "//src/core:metadata",
        "//src/core:metadata_batch",
    ],
)

# Test harnesses for filters. `FilterTest` is the harness for the v3 filter
# API: it builds an InterceptionChain, so it hosts both v3 filters and v3
# interceptors, and (via yodel) doubles every test as a fuzz target.
# `FilterTestV2` is the older harness for v2 filters; it goes away once there
# are no v2 filters left.
grpc_cc_library(
    name = "filter_test",
    testonly = 1,
    srcs = ["filter_test.cc"],
    hdrs = ["filter_test.h"],
    external_deps = [
        "absl/memory",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
        "fuzztest",
        "gtest",
    ],
    tags = ["nofixdeps"],  # until event engine tests are under fixbuilddeps
    visibility = ["//test:__subpackages__"],
    deps = [
        ":filter_matchers",
        "//:event_engine_base_hdrs",
        "//:gpr",
        "//:grpc",
        "//:grpc_base",
        "//:iomgr_timer",
        "//:ref_counted_ptr",
        "//src/core:activity",
        "//src/core:arena",
        "//src/core:arena_promise",
        "//src/core:basic_seq",
        "//src/core:call_finalization",
        "//src/core:call_spine",
        "//src/core:channel_args",
        "//src/core:context",
        "//src/core:default_event_engine",
        "//src/core:filter_args",
        "//src/core:interception_chain",
        "//src/core:map",
        "//src/core:memory_quota",
        "//src/core:message",
        "//src/core:metadata",
        "//src/core:metadata_batch",
        "//src/core:pipe",
        "//src/core:poll",
        "//src/core:promise_factory",
        "//src/core:resource_quota",
        "//src/core:seq",
        "//src/core:slice",
        "//src/core:slice_buffer",
        "//src/core:status_flag",
        "//test/core/call/yodel:yodel_test",
        "//test/core/event_engine/fuzzing_event_engine",
        "//test/core/event_engine/fuzzing_event_engine:fuzzing_event_engine_cc_proto",
    ],
)

# Self-tests for both harnesses in filter_test.{h,cc}. This is a fuzz test
# because every FilterTest case doubles as a fuzz target; the FilterTestV2 cases
# in the same file are ordinary TEST_F()s that fuzztest_main runs alongside them.
grpc_fuzz_test(
    name = "filter_test_test",
    srcs = ["filter_test_test.cc"],
    external_deps = [
        "absl/status",
        "absl/status:statusor",
        "fuzztest",
        "fuzztest_main",
        "gtest",
    ],
    deps = [
        "filter_test",
        ":filter_matchers",
        "//:event_engine_base_hdrs",
        "//:gpr",
        "//:grpc_base",
        "//:grpc_unsecure",
        "//:ref_counted_ptr",
        "//src/core:activity",
        "//src/core:arena_promise",
        "//src/core:channel_args",
        "//src/core:channelz_property_list",
        "//src/core:context",
        "//src/core:filter_args",
        "//src/core:interception_chain",
        "//src/core:map",
        "//src/core:message",
        "//src/core:metadata",
        "//src/core:metadata_batch",
        "//src/core:pipe",
        "//src/core:poll",
        "//src/core:seq",
        "//src/core:slice",
    ],
)

grpc_cc_test(
    name = "client_auth_filter_test",
    srcs = ["client_auth_filter_test.cc"],
    external_deps = [
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "gtest",
    ],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "filter_test",
        ":filter_matchers",
        "//:grpc",
        "//:grpc_base",
        "//:grpc_core_credentials_header",
        "//:grpc_security_base",
        "//:promise",
        "//:ref_counted_ptr",
        "//:transport_auth_context",
        "//src/core:arena_promise",
        "//src/core:channel_args",
        "//src/core:grpc_fake_credentials",
        "//src/core:metadata_batch",
        "//src/core:unique_type_name",
        "//src/core:useful",
    ],
)

grpc_fuzz_test(
    name = "client_authority_filter_test",
    srcs = ["client_authority_filter_test.cc"],
    external_deps = [
        "absl/status",
        "absl/strings",
        "fuzztest",
        "fuzztest_main",
        "gtest",
    ],
    deps = [
        "filter_test",
        ":filter_matchers",
        "//:channel_arg_names",
        "//src/core:channel_args",
        "//src/core:grpc_client_authority_filter",
        "//src/core:metadata",
    ],
)

grpc_fuzz_test(
    name = "message_size_filter_test",
    srcs = ["message_size_filter_test.cc"],
    external_deps = [
        "absl/status",
        "fuzztest",
        "fuzztest_main",
        "gtest",
    ],
    deps = [
        "filter_test",
        ":filter_matchers",
        "//:channel_arg_names",
        "//:gpr",
        "//src/core:channel_args",
        "//src/core:grpc_message_size_filter",
        "//src/core:metadata",
    ],
)

grpc_fuzz_test(
    name = "compression_filter_test",
    srcs = ["compression_filter_test.cc"],
    external_deps = [
        "absl/status",
        "fuzztest",
        "fuzztest_main",
        "gtest",
    ],
    deps = [
        "filter_test",
        ":filter_matchers",
        "//:channel_arg_names",
        "//:gpr",
        "//:grpc_http_filters",
        "//src/core:channel_args",
        "//src/core:metadata",
    ],
)

grpc_cc_test(
    name = "gcp_authentication_filter_test",
    srcs = ["gcp_authentication_filter_test.cc"],
    external_deps = [
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "gtest",
    ],
    tags = ["xds_test"],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "filter_test",
        ":filter_matchers",
        "//:grpc",
        "//:grpc_base",
        "//:grpc_security_base",
        "//:grpc_service_config_impl",
        "//:ref_counted_ptr",
        "//src/core:channel_args",
        "//src/core:gcp_authentication_filter",
        "//src/core:gcp_service_account_identity_credentials",
        "//src/core:grpc_resolver_xds_attributes",
        "//src/core:grpc_service_config",
        "//src/core:unique_type_name",
        "//src/core:xds_config",
    ],
)

grpc_cc_test(
    name = "recv_message_filter_bypass_test",
    srcs = ["recv_message_filter_bypass_test.cc"],
    external_deps = [
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "gtest",
    ],
    deps = [
        "//:call_combiner",
        "//:config",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc",
        "//:grpc_base",
        "//:grpc_unsecure",
        "//:ref_counted_ptr",
        "//src/core:activity",
        "//src/core:arena",
        "//src/core:arena_promise",
        "//src/core:channel_args",
        "//src/core:channel_args_preconditioning",
        "//src/core:closure",
        "//src/core:context",
        "//src/core:experiments",
        "//src/core:message",
        "//src/core:metadata_batch",
        "//src/core:poll",
        "//src/core:slice",
        "//src/core:slice_buffer",
        "//src/core:status_helper",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_benchmark(
    name = "bm_http_client_filter",
    srcs = ["bm_http_client_filter.cc"],
    external_deps = [
        "absl/strings",
    ],
    monitoring = HISTORY,
    deps = [
        "//:grpc",
        "//:grpc_base",
        "//:grpc_http_filters",
        "//src/core:connectivity_state",
        "//src/core:default_event_engine",
        "//src/core:metadata",
        "//test/core/call:call_spine_benchmarks",
    ],
)
