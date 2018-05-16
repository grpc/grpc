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

licenses(["notice"])  # Apache v2

load("//bazel:grpc_build_system.bzl", "grpc_cc_test", "grpc_cc_library", "grpc_cc_binary", "grpc_package")

grpc_package(name = "test/cpp/microbenchmarks")

grpc_cc_test(
    name = "noop-benchmark",
    srcs = ["noop-benchmark.cc"],
    external_deps = [
        "benchmark",
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
        "//test/core/util:grpc_test_util_unsecure",
        "//test/cpp/util:test_config",
    ],
)

grpc_cc_binary(
    name = "bm_closure",
    testonly = 1,
    srcs = ["bm_closure.cc"],
    deps = [":helpers"],
)

grpc_cc_binary(
    name = "bm_cq",
    testonly = 1,
    srcs = ["bm_cq.cc"],
    deps = [":helpers"],
)

grpc_cc_binary(
    name = "bm_cq_multiple_threads",
    testonly = 1,
    srcs = ["bm_cq_multiple_threads.cc"],
    deps = [":helpers"],
)

grpc_cc_binary(
    name = "bm_error",
    testonly = 1,
    srcs = ["bm_error.cc"],
    deps = [":helpers"],
)

grpc_cc_library(
    name = "fullstack_streaming_ping_pong_h",
    testonly = 1,
    hdrs = [
        "fullstack_streaming_ping_pong.h",
    ],
    deps = [":helpers"],
)

grpc_cc_binary(
    name = "bm_fullstack_streaming_ping_pong",
    testonly = 1,
    srcs = [
        "bm_fullstack_streaming_ping_pong.cc",
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

grpc_cc_binary(
    name = "bm_fullstack_streaming_pump",
    testonly = 1,
    srcs = [
        "bm_fullstack_streaming_pump.cc",
    ],
    deps = [":fullstack_streaming_pump_h"],
)

grpc_cc_binary(
    name = "bm_fullstack_trickle",
    testonly = 1,
    srcs = ["bm_fullstack_trickle.cc"],
    deps = [":helpers"],
)

grpc_cc_library(
    name = "fullstack_unary_ping_pong_h",
    testonly = 1,
    hdrs = [
        "fullstack_unary_ping_pong.h",
    ],
    deps = [":helpers"],
)

grpc_cc_binary(
    name = "bm_fullstack_unary_ping_pong",
    testonly = 1,
    srcs = [
        "bm_fullstack_unary_ping_pong.cc",
    ],
    deps = [":fullstack_unary_ping_pong_h"],
)

grpc_cc_binary(
    name = "bm_metadata",
    testonly = 1,
    srcs = ["bm_metadata.cc"],
    deps = [":helpers"],
)

grpc_cc_binary(
    name = "bm_chttp2_hpack",
    testonly = 1,
    srcs = ["bm_chttp2_hpack.cc"],
    deps = [":helpers"],
)
