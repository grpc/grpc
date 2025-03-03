# Copyright 2017 gRPC authors.
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

load("//bazel:grpc_build_system.bzl", "grpc_cc_library", "grpc_package")
load("//test/cpp/microbenchmarks:grpc_benchmark_config.bzl", "HISTORY", "grpc_cc_benchmark")

licenses(["notice"])

grpc_package(name = "test/cpp/microbenchmarks")

grpc_cc_benchmark(
    name = "noop-benchmark",
    srcs = ["noop-benchmark.cc"],
    deps = ["//test/core/test_util:grpc_test_util"],
)

grpc_cc_benchmark(
    name = "bm_channel_args",
    srcs = ["bm_channel_args.cc"],
    external_deps = [
        "absl/container:btree",
    ],
    monitoring = HISTORY,
    deps = [
        "//:grpc++",
        "//src/core:channel_args",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_benchmark(
    name = "bm_rng",
    srcs = ["bm_rng.cc"],
    external_deps = [
        "absl/container:btree",
    ],
    monitoring = HISTORY,
    deps = [
        "//:grpc++",
        "//src/core:channel_args",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_benchmark(
    name = "bm_exec_ctx",
    srcs = ["bm_exec_ctx.cc"],
    uses_event_engine = False,
    deps = [":helpers"],
)

grpc_cc_benchmark(
    name = "bm_event_engine_run",
    srcs = ["bm_event_engine_run.cc"],
    external_deps = [
        "absl/log:check",
        "absl/debugging:leak_check",
    ],
    deps = [
        ":helpers",
        "//src/core:common_event_engine_closures",
    ],
)

grpc_cc_benchmark(
    name = "bm_thread_pool",
    srcs = ["bm_thread_pool.cc"],
    external_deps = [
        "absl/log:check",
    ],
    monitoring = HISTORY,
    uses_event_engine = False,
    deps = [
        ":helpers",
        "//src/core:common_event_engine_closures",
    ],
)

grpc_cc_library(
    name = "helpers",
    testonly = 1,
    srcs = ["helpers.cc"],
    hdrs = [
        "fullstack_context_mutators.h",
        "fullstack_fixtures.h",
        "helpers.h",
    ],
    external_deps = [
        "absl/log:check",
        "benchmark",
    ],
    deps = [
        "//:grpc++",
        "//src/proto/grpc/testing:echo_cc_grpc",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
        "//test/cpp/util:test_config",
    ],
)

# Need a secure version of helpers to benchmark opencensus
grpc_cc_library(
    name = "helpers_secure",
    testonly = 1,
    srcs = ["helpers.cc"],
    hdrs = [
        "fullstack_context_mutators.h",
        "fullstack_fixtures.h",
        "helpers.h",
    ],
    external_deps = [
        "absl/log:check",
        "benchmark",
    ],
    deps = [
        "//:grpc++",
        "//src/proto/grpc/testing:echo_cc_grpc",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
        "//test/cpp/util:test_config",
    ],
)

grpc_cc_benchmark(
    name = "bm_closure",
    srcs = ["bm_closure.cc"],
    monitoring = HISTORY,
    deps = [
        ":helpers",
        "//src/core:closure",
    ],
)

grpc_cc_benchmark(
    name = "bm_huffman_decode",
    srcs = ["bm_huffman_decode.cc"],
    tags = [
        "nomsan",
        "notsan",
        "noubsan",
    ],
    deps = [
        ":helpers",
        "//test/cpp/microbenchmarks/huffman_geometries",
    ],
)

grpc_cc_benchmark(
    name = "bm_alarm",
    srcs = ["bm_alarm.cc"],
    monitoring = HISTORY,
    deps = [":helpers"],
)

grpc_cc_benchmark(
    name = "bm_arena",
    srcs = ["bm_arena.cc"],
    tags = [
        "notsan",
    ],
    uses_event_engine = False,
    deps = [":helpers"],
)

grpc_cc_benchmark(
    name = "bm_byte_buffer",
    srcs = ["bm_byte_buffer.cc"],
    external_deps = [
        "absl/log:check",
    ],
    tags = [
        "no_mac",
        "no_windows",
    ],
    uses_event_engine = False,
    deps = [":helpers"],
)

grpc_cc_benchmark(
    name = "bm_channel",
    srcs = ["bm_channel.cc"],
    monitoring = HISTORY,
    tags = [
        "no_mac",
        "no_windows",
    ],
    uses_event_engine = False,
    deps = [":helpers"],
)

grpc_cc_benchmark(
    name = "bm_cq",
    srcs = ["bm_cq.cc"],
    external_deps = [
        "absl/log:check",
    ],
    monitoring = HISTORY,
    deps = [":helpers"],
)

grpc_cc_benchmark(
    name = "bm_cq_multiple_threads",
    srcs = ["bm_cq_multiple_threads.cc"],
    external_deps = [
        "absl/log:check",
    ],
    uses_event_engine = False,
    deps = [":helpers"],
)

grpc_cc_library(
    name = "fullstack_streaming_ping_pong_h",
    testonly = 1,
    hdrs = [
        "fullstack_streaming_ping_pong.h",
    ],
    external_deps = [
        "absl/log:check",
    ],
    tags = [
        "no_mac",
        "no_windows",
    ],
    deps = [":helpers"],
)

grpc_cc_benchmark(
    name = "bm_fullstack_streaming_ping_pong",
    size = "large",
    srcs = [
        "bm_fullstack_streaming_ping_pong.cc",
    ],
    flaky = True,
    deps = [":fullstack_streaming_ping_pong_h"],
)

grpc_cc_library(
    name = "fullstack_streaming_pump_h",
    testonly = 1,
    hdrs = [
        "fullstack_streaming_pump.h",
    ],
    external_deps = [
        "absl/log:check",
    ],
    deps = [":helpers"],
)

grpc_cc_benchmark(
    name = "bm_fullstack_streaming_pump",
    srcs = [
        "bm_fullstack_streaming_pump.cc",
    ],
    deps = [":fullstack_streaming_pump_h"],
)

grpc_cc_library(
    name = "fullstack_unary_ping_pong_h",
    testonly = 1,
    hdrs = [
        "fullstack_unary_ping_pong.h",
    ],
    external_deps = [
        "absl/log:check",
    ],
    deps = [":helpers_secure"],
)

grpc_cc_benchmark(
    name = "bm_fullstack_unary_ping_pong",
    size = "large",
    srcs = [
        "bm_fullstack_unary_ping_pong.cc",
    ],
    deps = [":fullstack_unary_ping_pong_h"],
)

grpc_cc_benchmark(
    name = "bm_fullstack_unary_ping_pong_chaotic_good",
    srcs = [
        "bm_fullstack_unary_ping_pong_chaotic_good.cc",
    ],
    deps = [
        ":fullstack_unary_ping_pong_h",
        "//:grpcpp_chaotic_good",
    ],
)

grpc_cc_benchmark(
    name = "bm_chttp2_hpack",
    srcs = ["bm_chttp2_hpack.cc"],
    external_deps = [
        "absl/log:check",
    ],
    uses_event_engine = False,
    deps = [
        ":helpers",
        "//src/core:slice",
    ],
)

grpc_cc_benchmark(
    name = "bm_opencensus_plugin",
    srcs = ["bm_opencensus_plugin.cc"],
    tags = [
        "grpc:fails-internally",
    ],
    deps = [
        ":helpers_secure",
        "//:grpc_opencensus_plugin",
        "//src/proto/grpc/testing:echo_cc_grpc",
    ],
)

grpc_cc_library(
    name = "bm_callback_test_service_impl",
    testonly = 1,
    srcs = ["callback_test_service.cc"],
    hdrs = ["callback_test_service.h"],
    external_deps = [
        "absl/log:check",
        "benchmark",
    ],
    deps = [
        ":helpers",
        "//src/proto/grpc/testing:echo_cc_grpc",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_library(
    name = "callback_unary_ping_pong_h",
    testonly = 1,
    hdrs = [
        "callback_unary_ping_pong.h",
    ],
    external_deps = [
        "absl/log:check",
    ],
    deps = [
        ":bm_callback_test_service_impl",
        ":helpers",
    ],
)

grpc_cc_benchmark(
    name = "bm_callback_unary_ping_pong",
    size = "large",
    srcs = [
        "bm_callback_unary_ping_pong.cc",
    ],
    deps = [":callback_unary_ping_pong_h"],
)

grpc_cc_library(
    name = "callback_streaming_ping_pong_h",
    testonly = 1,
    hdrs = [
        "callback_streaming_ping_pong.h",
    ],
    external_deps = [
        "absl/log:check",
    ],
    deps = [
        ":bm_callback_test_service_impl",
        ":helpers",
    ],
)

grpc_cc_benchmark(
    name = "bm_callback_streaming_ping_pong",
    srcs = [
        "bm_callback_streaming_ping_pong.cc",
    ],
    deps = [":callback_streaming_ping_pong_h"],
)

# TODO(hork): Generalize this for other work queue implementations
grpc_cc_benchmark(
    name = "bm_basic_work_queue",
    srcs = ["bm_basic_work_queue.cc"],
    external_deps = [
        "absl/log:check",
    ],
    tags = [
        "manual",
        "notap",
    ],
    deps = [
        "//:gpr",
        "//src/core:common_event_engine_closures",
        "//src/core:event_engine_basic_work_queue",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_benchmark(
    name = "bm_stats_plugin",
    srcs = ["bm_stats_plugin.cc"],
    external_deps = [
        "absl/strings",
        "otel/sdk/src/metrics",
    ],
    monitoring = HISTORY,
    tags = [
        # TODO(b/332369798): Remove after fixing bug
        "grpc:otel-namespace-calamity",
    ],
    deps = [
        ":helpers",
        "//src/core:metrics",
        "//src/cpp/ext/otel:otel_plugin",
        "//test/core/test_util:fake_stats_plugin",
        "//test/core/test_util:grpc_test_util",
    ],
)
