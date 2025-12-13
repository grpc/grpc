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
load("//test/cpp/microbenchmarks:grpc_benchmark_config.bzl", "HISTORY", "grpc_cc_benchmark")

licenses(["notice"])

grpc_package(
    name = "test/core/filters",
    features = [
        "layering_check",
        "parse_headers",
    ],
)

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
        "gtest",
    ],
    tags = ["nofixdeps"],  # until event engine tests are under fixbuilddeps
    deps = [
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
        "//src/core:channel_args",
        "//src/core:context",
        "//src/core:default_event_engine",
        "//src/core:memory_quota",
        "//src/core:metadata_batch",
        "//src/core:pipe",
        "//src/core:poll",
        "//src/core:resource_quota",
        "//src/core:seq",
        "//src/core:slice",
        "//src/core:slice_buffer",
        "//test/core/event_engine/fuzzing_event_engine",
        "//test/core/event_engine/fuzzing_event_engine:fuzzing_event_engine_cc_proto",
    ],
)

grpc_cc_test(
    name = "filter_test_test",
    srcs = ["filter_test_test.cc"],
    external_deps = [
        "absl/status:statusor",
        "gtest",
    ],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "filter_test",
        "//:grpc_base",
        "//:grpc_unsecure",
        "//src/core:activity",
        "//src/core:arena_promise",
        "//src/core:channel_args",
        "//src/core:map",
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

grpc_cc_test(
    name = "client_authority_filter_test",
    srcs = ["client_authority_filter_test.cc"],
    external_deps = [
        "absl/status",
        "absl/strings",
        "gtest",
    ],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "filter_test",
        "//:channel_arg_names",
        "//src/core:grpc_client_authority_filter",
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
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "filter_test",
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
    name = "composite_filter_test",
    srcs = ["composite_filter_test.cc"],
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
        "//:grpc",
        "//:ref_counted_ptr",
        "//src/core:channel_args",
        "//src/core:composite_filter",
        "//src/core:unique_type_name",
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

grpc_cc_test(
    name = "blackboard_test",
    srcs = ["blackboard_test.cc"],
    external_deps = [
        "gtest",
    ],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//src/core:blackboard",
    ],
)
