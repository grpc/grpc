# Copyright 2026 gRPC authors.
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

load(
    "//bazel:grpc_build_system.bzl",
    "grpc_cc_library",
    "grpc_package",
)
load("//test/core/test_util:grpc_fuzzer.bzl", "grpc_fuzz_test")

grpc_package(
    name = "test/core/filters/v3_filter_test",
    features = [
        "layering_check",
        "parse_headers",
    ],
)

# Native v3 filter test harness. Builds a CallFilters::Stack and runs it on a
# real CallSpine, and (via yodel) doubles every test as a fuzzer.
grpc_cc_library(
    name = "v3_filter_test",
    testonly = 1,
    srcs = ["v3_filter_test.cc"],
    hdrs = ["v3_filter_test.h"],
    external_deps = [
        "absl/functional:any_invocable",
        "absl/log:check",
        "absl/status",
        "absl/strings",
        "gtest",
        "fuzztest",
    ],
    deps = [
        "//:event_engine_base_hdrs",
        "//:gpr",
        "//:grpc_base",
        "//:ref_counted_ptr",
        "//src/core:call_filters",
        "//src/core:call_spine",
        "//src/core:channel_args",
        "//src/core:filter_args",
        "//src/core:message",
        "//src/core:metadata",
        "//src/core:metadata_batch",
        "//test/core/call/yodel:yodel_test",
    ],
)

grpc_fuzz_test(
    name = "client_authority_filter_test",
    srcs = ["client_authority_filter_test.cc"],
    external_deps = [
        "absl/strings",
        "gtest",
        "fuzztest",
        "fuzztest_main",
    ],
    deps = [
        ":v3_filter_test",
        "//:gpr",
        "//:grpc_base",
        "//src/core:arena",
        "//src/core:channel_args",
        "//src/core:grpc_client_authority_filter",
        "//src/core:metadata",
        "//src/core:slice",
    ],
)

grpc_fuzz_test(
    name = "v3_filter_test_test",
    srcs = ["v3_filter_test_test.cc"],
    external_deps = [
        "absl/status",
        "absl/status:statusor",
        "gtest",
        "fuzztest",
        "fuzztest_main",
    ],
    deps = [
        ":v3_filter_test",
        "//:gpr",
        "//:grpc_base",
        "//src/core:arena",
        "//src/core:channel_args",
        "//src/core:channelz_property_list",
        "//src/core:message",
        "//src/core:metadata",
        "//src/core:slice",
        "//src/core:slice_buffer",
        "//src/core:status_flag",
        "//test/core/filters:filter_matchers",
    ],
)

grpc_fuzz_test(
    name = "message_size_filter_test",
    srcs = ["message_size_filter_test.cc"],
    external_deps = [
        "absl/status",
        "gtest",
        "fuzztest",
        "fuzztest_main",
    ],
    deps = [
        ":v3_filter_test",
        "//:gpr",
        "//:grpc_base",
        "//src/core:arena",
        "//src/core:channel_args",
        "//src/core:grpc_message_size_filter",
        "//src/core:message",
        "//src/core:metadata",
        "//src/core:slice",
        "//src/core:slice_buffer",
        "//src/core:status_flag",
        "//test/core/filters:filter_matchers",
    ],
)



grpc_fuzz_test(
    name = "compression_filter_test",
    srcs = ["compression_filter_test.cc"],
    external_deps = [
        "absl/status",
        "gtest",
        "fuzztest",
        "fuzztest_main",
    ],
    deps = [
        ":v3_filter_test",
        "//:gpr",
        "//:grpc_base",
        "//:grpc_http_filters",
        "//src/core:arena",
        "//src/core:channel_args",
        "//src/core:message",
        "//src/core:metadata",
        "//src/core:slice",
        "//src/core:slice_buffer",
        "//src/core:status_flag",
        "//test/core/filters:filter_matchers",
    ],
)
