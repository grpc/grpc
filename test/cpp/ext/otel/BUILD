# Copyright 2023 gRPC authors.
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

licenses(["notice"])

grpc_package(
    name = "test/cpp/ext/otel",
    visibility = "tests",
)

grpc_cc_library(
    name = "otel_test_library",
    testonly = 1,
    srcs = [
        "otel_test_library.cc",
    ],
    hdrs = [
        "otel_test_library.h",
    ],
    external_deps = [
        "gtest",
        "otel/api",
        "otel/sdk/src/metrics",
    ],
    tags = [
    ],
    deps = [
        "//:grpc++",
        "//src/cpp/ext/otel:otel_plugin",
        "//test/core/test_util:fake_stats_plugin",
        "//test/core/test_util:grpc_test_util",
        "//test/cpp/end2end:test_service_impl",
    ],
)

grpc_cc_test(
    name = "otel_plugin_test",
    srcs = [
        "otel_plugin_test.cc",
    ],
    external_deps = [
        "gtest",
        "otel/api",
        "otel/sdk/src/metrics",
    ],
    tags = [
        # TODO(b/398739502): Remove after fixing
        "grpc:broken-internally",
        # TODO(b/332369798): Remove after fixing bug
        "grpc:otel-namespace-calamity",
    ],
    deps = [
        ":otel_test_library",
        "//:grpc++",
        "//src/cpp/ext/otel:otel_plugin",
        "//test/core/promise:test_context",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "grpc_text_map_carrier_test",
    srcs = [
        "grpc_text_map_carrier_test.cc",
    ],
    external_deps = [
        "gtest",
        "otel/api",
    ],
    tags = [],
    deps = [
        "//:grpc++",
        "//src/cpp/ext/otel:otel_plugin",
        "//test/core/promise:test_context",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "grpc_trace_bin_text_map_propagator_test",
    srcs = [
        "grpc_trace_bin_text_map_propagator_test.cc",
    ],
    external_deps = [
        "gtest",
        "otel/api",
    ],
    tags = [
        # TODO(b/332369798): Remove after fixing bug
        "grpc:otel-namespace-calamity",
    ],
    deps = [
        "//:grpc++",
        "//src/cpp/ext/otel:otel_plugin",
        "//test/core/promise:test_context",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "otel_tracing_test",
    srcs = [
        "otel_tracing_test.cc",
    ],
    external_deps = [
        "gtest",
        "otel/api",
        "otel/exporters/memory:in_memory_span_exporter",
        "otel/sdk/src/trace",
    ],
    tags = [
        # TODO(b/398739502): Remove after fixing
        "grpc:broken-internally",
        # TODO(b/332369798): Remove after fixing bug
        "grpc:otel-namespace-calamity",
    ],
    deps = [
        ":otel_test_library",
        "//:grpc++",
        "//src/cpp/ext/otel:otel_plugin",
        "//test/core/promise:test_context",
        "//test/core/test_util:fail_first_call_filter",
        "//test/core/test_util:grpc_test_util",
    ],
)
