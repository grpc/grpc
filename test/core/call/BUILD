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
load("//test/cpp/microbenchmarks:grpc_benchmark_config.bzl", "HISTORY", "grpc_cc_benchmark")

grpc_package(name = "test/core/call")

grpc_fuzz_test(
    name = "client_call_test",
    srcs = [
        "client_call_test.cc",
    ],
    external_deps = [
        "fuzztest",
        "fuzztest_main",
        "gtest",
    ],
    deps = [
        "batch_builder",
        "//:grpc_base",
        "//test/core/call/yodel:yodel_test",
    ],
)

grpc_fuzz_test(
    name = "server_call_test",
    srcs = [
        "server_call_test.cc",
    ],
    external_deps = [
        "fuzztest",
        "fuzztest_main",
        "gtest",
    ],
    deps = [
        "batch_builder",
        "//:grpc_base",
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
        "//src/core:default_event_engine",
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
    external_deps = ["gtest"],
    deps = [
        "//src/core:filter_fusion",
    ],
)
