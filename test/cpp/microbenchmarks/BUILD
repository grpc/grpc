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

load("//bazel:grpc_build_system.bzl", "grpc_cc_library", "grpc_cc_test", "grpc_package")
load("//test/cpp/microbenchmarks:grpc_benchmark_config.bzl", "grpc_benchmark_args")

licenses(["notice"])

grpc_package(name = "test/cpp/microbenchmarks")

grpc_cc_test(
    name = "noop-benchmark",
    srcs = ["noop-benchmark.cc"],
    external_deps = [
        "benchmark",
    ],
    deps = ["//test/core/util:grpc_test_util"],
)

grpc_cc_test(
    name = "bm_channel_args",
    srcs = ["bm_channel_args.cc"],
    external_deps = [
        "benchmark",
        "absl/container:btree",
    ],
    deps = [
        "//:grpc++",
        "//src/core:channel_args",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "bm_rng",
    srcs = ["bm_rng.cc"],
    external_deps = [
        "benchmark",
        "absl/container:btree",
    ],
    deps = [
        "//:grpc++",
        "//src/core:channel_args",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "bm_exec_ctx",
    srcs = ["bm_exec_ctx.cc"],
    args = grpc_benchmark_args(),
    external_deps = [
        "benchmark",
    ],
    uses_event_engine = False,
    uses_polling = False,
    deps = [":helpers"],
)

grpc_cc_test(
    name = "bm_event_engine_run",
    size = "small",
    srcs = ["bm_event_engine_run.cc"],
    args = grpc_benchmark_args(),
    external_deps = [
        "absl/debugging:leak_check",
        "benchmark",
    ],
    uses_polling = False,
    deps = [
        ":helpers",
        "//src/core:common_event_engine_closures",
    ],
)

grpc_cc_test(
    name = "bm_thread_pool",
    size = "small",
    srcs = ["bm_thread_pool.cc"],
    args = grpc_benchmark_args(),
    external_deps = [
        "benchmark",
    ],
    uses_event_engine = False,
    uses_polling = False,
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
        "benchmark",
    ],
    deps = [
        "//:grpc++_unsecure",
        "//src/proto/grpc/testing:echo_proto",
        "//test/core/util:grpc_test_util_base",
        "//test/core/util:grpc_test_util_unsecure",
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
        "benchmark",
    ],
    deps = [
        "//:grpc++",
        "//src/proto/grpc/testing:echo_proto",
        "//test/core/util:grpc_test_util",
        "//test/core/util:grpc_test_util_base",
        "//test/cpp/util:test_config",
    ],
)

grpc_cc_test(
    name = "bm_closure",
    srcs = ["bm_closure.cc"],
    args = grpc_benchmark_args(),
    tags = [
        "no_mac",
        "no_windows",
    ],
    deps = [
        ":helpers",
        "//src/core:closure",
    ],
)

grpc_cc_test(
    name = "bm_huffman_decode",
    srcs = ["bm_huffman_decode.cc"],
    args = grpc_benchmark_args(),
    tags = [
        "no_mac",
        "no_windows",
        "nomsan",
        "notsan",
        "noubsan",
    ],
    deps = [
        ":helpers",
        "//test/cpp/microbenchmarks/huffman_geometries",
    ],
)

grpc_cc_test(
    name = "bm_alarm",
    srcs = ["bm_alarm.cc"],
    args = grpc_benchmark_args(),
    tags = [
        "no_mac",
        "no_windows",
    ],
    deps = [":helpers"],
)

grpc_cc_test(
    name = "bm_arena",
    size = "large",
    srcs = ["bm_arena.cc"],
    args = grpc_benchmark_args(),
    tags = [
        "no_mac",
        "no_windows",
        "notsan",
    ],
    uses_event_engine = False,
    uses_polling = False,
    deps = [":helpers"],
)

grpc_cc_test(
    name = "bm_byte_buffer",
    srcs = ["bm_byte_buffer.cc"],
    args = grpc_benchmark_args(),
    tags = [
        "no_mac",
        "no_windows",
    ],
    uses_event_engine = False,
    uses_polling = False,
    deps = [":helpers"],
)

grpc_cc_test(
    name = "bm_channel",
    srcs = ["bm_channel.cc"],
    args = grpc_benchmark_args(),
    tags = [
        "no_mac",
        "no_windows",
    ],
    uses_event_engine = False,
    uses_polling = False,
    deps = [":helpers"],
)

grpc_cc_test(
    name = "bm_call_create",
    srcs = ["bm_call_create.cc"],
    args = grpc_benchmark_args(),
    tags = [
        "no_mac",
        "no_windows",
    ],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        ":helpers",
        "//src/core:channel_args",
    ],
)

grpc_cc_test(
    name = "bm_cq",
    srcs = ["bm_cq.cc"],
    args = grpc_benchmark_args(),
    tags = [
        "no_mac",
        "no_windows",
    ],
    deps = [":helpers"],
)

grpc_cc_test(
    name = "bm_cq_multiple_threads",
    srcs = ["bm_cq_multiple_threads.cc"],
    args = grpc_benchmark_args(),
    tags = [
        "no_mac",
        "no_windows",
    ],
    uses_event_engine = False,
    uses_polling = False,
    deps = [":helpers"],
)

grpc_cc_library(
    name = "fullstack_streaming_ping_pong_h",
    testonly = 1,
    hdrs = [
        "fullstack_streaming_ping_pong.h",
    ],
    tags = [
        "no_mac",
        "no_windows",
    ],
    deps = [":helpers"],
)

grpc_cc_test(
    name = "bm_fullstack_streaming_ping_pong",
    size = "large",
    srcs = [
        "bm_fullstack_streaming_ping_pong.cc",
    ],
    args = grpc_benchmark_args(),
    flaky = True,
    tags = [
        "no_mac",  # to emulate "excluded_poll_engines: poll"
        "no_windows",
    ],
    deps = [":fullstack_streaming_ping_pong_h"],
)

grpc_cc_library(
    name = "fullstack_streaming_pump_h",
    testonly = 1,
    hdrs = [
        "fullstack_streaming_pump.h",
    ],
    deps = [":helpers"],
)

grpc_cc_test(
    name = "bm_fullstack_streaming_pump",
    srcs = [
        "bm_fullstack_streaming_pump.cc",
    ],
    args = grpc_benchmark_args(),
    tags = [
        "no_mac",  # to emulate "excluded_poll_engines: poll"
        "no_windows",
    ],
    deps = [":fullstack_streaming_pump_h"],
)

grpc_cc_library(
    name = "fullstack_unary_ping_pong_h",
    testonly = 1,
    hdrs = [
        "fullstack_unary_ping_pong.h",
    ],
    deps = [":helpers"],
)

grpc_cc_test(
    name = "bm_fullstack_unary_ping_pong",
    size = "large",
    srcs = [
        "bm_fullstack_unary_ping_pong.cc",
    ],
    args = grpc_benchmark_args(),
    tags = [
        "no_mac",  # to emulate "excluded_poll_engines: poll"
        "no_windows",
    ],
    deps = [":fullstack_unary_ping_pong_h"],
)

grpc_cc_test(
    name = "bm_chttp2_hpack",
    srcs = ["bm_chttp2_hpack.cc"],
    args = grpc_benchmark_args(),
    tags = [
        "no_mac",
        "no_windows",
    ],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        ":helpers",
        "//src/core:slice",
    ],
)

grpc_cc_test(
    name = "bm_chttp2_transport",
    srcs = ["bm_chttp2_transport.cc"],
    args = grpc_benchmark_args(),
    tags = [
        "no_mac",
        "no_windows",
        "nomsan",
    ],
    deps = [
        ":helpers",
        "//src/core:closure",
        "//src/core:slice",
    ],
)

grpc_cc_test(
    name = "bm_opencensus_plugin",
    srcs = ["bm_opencensus_plugin.cc"],
    args = grpc_benchmark_args(),
    language = "C++",
    deps = [
        ":helpers_secure",
        "//:grpc_opencensus_plugin",
        "//src/proto/grpc/testing:echo_proto",
    ],
)

grpc_cc_test(
    name = "bm_pollset",
    srcs = ["bm_pollset.cc"],
    args = grpc_benchmark_args(),
    tags = [
        "no_mac",
        "no_windows",
    ],
    deps = [
        ":helpers",
    ],
)

grpc_cc_library(
    name = "bm_callback_test_service_impl",
    testonly = 1,
    srcs = ["callback_test_service.cc"],
    hdrs = ["callback_test_service.h"],
    external_deps = [
        "benchmark",
    ],
    deps = [
        ":helpers",
        "//src/proto/grpc/testing:echo_proto",
        "//test/cpp/util:test_util_unsecure",
    ],
)

grpc_cc_library(
    name = "callback_unary_ping_pong_h",
    testonly = 1,
    hdrs = [
        "callback_unary_ping_pong.h",
    ],
    deps = [
        ":bm_callback_test_service_impl",
        ":helpers",
    ],
)

grpc_cc_test(
    name = "bm_callback_unary_ping_pong",
    size = "large",
    srcs = [
        "bm_callback_unary_ping_pong.cc",
    ],
    args = grpc_benchmark_args(),
    tags = [
        "manual",
        "no_mac",
        "no_windows",
        "notap",
    ],
    deps = [":callback_unary_ping_pong_h"],
)

grpc_cc_library(
    name = "callback_streaming_ping_pong_h",
    testonly = 1,
    hdrs = [
        "callback_streaming_ping_pong.h",
    ],
    deps = [
        ":bm_callback_test_service_impl",
        ":helpers",
    ],
)

grpc_cc_test(
    name = "bm_callback_streaming_ping_pong",
    size = "large",
    srcs = [
        "bm_callback_streaming_ping_pong.cc",
    ],
    args = grpc_benchmark_args(),
    tags = [
        "manual",
        "no_mac",
        "no_windows",
        "notap",
    ],
    deps = [":callback_streaming_ping_pong_h"],
)

# TODO(hork): Generalize this for other work queue implementations
grpc_cc_test(
    name = "bm_basic_work_queue",
    srcs = ["bm_basic_work_queue.cc"],
    args = grpc_benchmark_args(),
    external_deps = ["benchmark"],
    tags = [
        "manual",
        "no_windows",
        "notap",
    ],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:gpr",
        "//src/core:common_event_engine_closures",
        "//src/core:event_engine_basic_work_queue",
        "//test/core/util:grpc_test_util",
    ],
)
