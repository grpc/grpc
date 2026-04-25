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
    features = [
        "layering_check",
        "parse_headers",
    ],
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
        "absl/functional:any_invocable",
        "gtest",
        "otel/api",
        "otel/sdk/src/metrics",
        "otel/sdk:headers",
    ],
    tags = [
        # TODO(b/332369798): Remove after fixing bug
        "grpc:otel-namespace-calamity",
    ],
    deps = [
        "//:call_tracer",
        "//:config",
        "//:gpr",
        "//:grpc++",
        "//:grpc_base",
        "//src/core:notification",
        "//src/cpp/ext/otel:otel_plugin",
        "//test/core/test_util:fake_stats_plugin",
        "//test/core/test_util:grpc_test_util",
        "//test/cpp/end2end:test_service_impl",
        "//test/cpp/util:test_util",
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
        "absl/functional:any_invocable",
        "otel/sdk:headers",
    ],
    tags = [
        # Incompatible with internal tracing
        "grpc:no-internal-poller",
        # TODO(b/332369798): Remove after fixing bug
        "grpc:otel-namespace-calamity",
    ],
    deps = [
        ":otel_test_library",
        "//:call_tracer",
        "//:config",
        "//:grpc++",
        "//src/core:channel_args_endpoint_config",
        "//src/cpp/ext/otel:otel_plugin",
        "//test/core/promise:test_context",
        "//test/core/test_util:fail_first_call_filter",
        "//test/core/test_util:fake_stats_plugin",
        "//test/core/test_util:grpc_test_util",
        "//test/cpp/end2end:test_service_impl",
    ],
)

grpc_cc_test(
    name = "otel_plugin_option_test",
    srcs = [
        "otel_plugin_option_test.cc",
    ],
    external_deps = [
        "gtest",
        "otel/api",
        "otel/sdk/src/metrics",
        "absl/functional:any_invocable",
        "otel/sdk:headers",
    ],
    tags = [
        # Incompatible with internal tracing
        "grpc:no-internal-poller",
        # TODO(b/332369798): Remove after fixing bug
        "grpc:otel-namespace-calamity",
    ],
    deps = [
        ":otel_test_library",
        "//:call_tracer",
        "//:config",
        "//:grpc++",
        "//src/core:channel_args_endpoint_config",
        "//src/cpp/ext/otel:otel_plugin",
        "//test/core/promise:test_context",
        "//test/core/test_util:fake_stats_plugin",
        "//test/core/test_util:grpc_test_util",
        "//test/cpp/end2end:test_service_impl",
    ],
)

grpc_cc_test(
    name = "otel_npc_metrics_test",
    srcs = [
        "otel_npc_metrics_test.cc",
    ],
    external_deps = [
        "gtest",
        "otel/api",
        "otel/sdk/src/metrics",
        "otel/sdk:headers",
        "absl/functional:any_invocable",
    ],
    tags = [
        # TODO(b/332369798): Remove after fixing bug
        "grpc:otel-namespace-calamity",
    ],
    deps = [
        ":otel_test_library",
        "//:call_tracer",
        "//:config",
        "//:grpc++",
        "//:grpc++_base",
        "//:grpc_base",
        "//src/core:channel_args_endpoint_config",
        "//src/core:instrument",
        "//src/cpp/ext/otel:otel_plugin",
        "//test/core/promise:test_context",
        "//test/core/test_util:fake_stats_plugin",
        "//test/core/test_util:grpc_test_util",
        "//test/cpp/end2end:test_service_impl",
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
        "//src/core:metadata_batch",
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
        "//src/core:metadata_batch",
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
        "otel/sdk:headers",
        "absl/synchronization",
    ],
    tags = [
        # Incompatible with internal tracing
        "grpc:no-internal-poller",
        # TODO(b/332369798): Remove after fixing bug
        "grpc:otel-namespace-calamity",
    ],
    deps = [
        ":otel_test_library",
        "//:call_tracer",
        "//:config",
        "//:gpr",
        "//:grpc++",
        "//:grpc_transport_chttp2",
        "//src/core:posix_event_engine_event_poller",
        "//src/core:posix_event_engine_poller_posix_default",
        "//src/cpp/ext/otel:otel_plugin",
        "//test/core/promise:test_context",
        "//test/core/test_util:fail_first_call_filter",
        "//test/core/test_util:fake_stats_plugin",
        "//test/core/test_util:grpc_test_util",
        "//test/cpp/end2end:test_service_impl",
    ],
)
