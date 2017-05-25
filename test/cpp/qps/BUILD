# Copyright 2017, Google Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

licenses(["notice"])  # 3-clause BSD

load("//bazel:grpc_build_system.bzl", "grpc_cc_test", "grpc_cc_library", "grpc_cc_binary")

package(
    features = [
        "-layering_check",
        "-parse_headers",
    ],
)

grpc_cc_library(
    name = "parse_json",
    srcs = ["parse_json.cc"],
    hdrs = ["parse_json.h"],
    deps = ["//:grpc++"],
    external_deps = ["protobuf"],
)

grpc_cc_library(
    name = "qps_worker_impl",
    srcs = [
        "client_async.cc",
        "client_sync.cc",
        "qps_worker.cc",
        "server_async.cc",
        "server_sync.cc",
    ],
    hdrs = [
        "client.h",
        "qps_worker.h",
        "server.h",
    ],
    deps = [
        ":histogram",
        ":interarrival",
        ":usage_timer",
        "//:grpc",
        "//:grpc++",
        "//src/proto/grpc/testing:control_proto",
        "//src/proto/grpc/testing:payloads_proto",
        "//src/proto/grpc/testing:services_proto",
        "//test/core/end2end:ssl_test_data",
        "//test/core/util:gpr_test_util",
        "//test/core/util:grpc_test_util",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_library(
    name = "driver_impl",
    srcs = [
        "driver.cc",
        "report.cc",
    ],
    hdrs = [
        "driver.h",
        "report.h",
    ],
    deps = [
        ":histogram",
        ":parse_json",
        ":qps_worker_impl",
        "//:grpc++",
        "//src/proto/grpc/testing:control_proto",
        "//src/proto/grpc/testing:messages_proto",
        "//src/proto/grpc/testing:services_proto",
        "//test/core/util:gpr_test_util",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_library(
    name = "benchmark_config",
    srcs = [
        "benchmark_config.cc",
    ],
    hdrs = [
        "benchmark_config.h",
    ],
    deps = [
        ":driver_impl",
        ":histogram",
        "//:grpc++",
        "//src/proto/grpc/testing:control_proto",
    ],
    external_deps = [
        "gflags",
    ],
)

grpc_cc_library(
    name = "histogram",
    hdrs = [
        "histogram.h",
        "stats.h",
    ],
    deps = ["//:gpr"],
)

grpc_cc_library(
    name = "interarrival",
    hdrs = ["interarrival.h"],
    deps = ["//:grpc++"],
)

grpc_cc_binary(
    name = "json_run_localhost",
    srcs = ["json_run_localhost.cc"],
    deps = [
        "//:gpr",
        "//test/core/util:gpr_test_util",
        "//test/core/util:grpc_test_util",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_test(
    name = "qps_interarrival_test",
    srcs = ["qps_interarrival_test.cc"],
    deps = [
        ":histogram",
        ":interarrival",
    ],
)

grpc_cc_binary(
    name = "qps_json_driver",
    srcs = ["qps_json_driver.cc"],
    deps = [
        ":benchmark_config",
        ":driver_impl",
        "//:grpc++",
    ],
    external_deps = [
        "gflags",
    ],
)

grpc_cc_test(
    name = "qps_openloop_test",
    srcs = ["qps_openloop_test.cc"],
    deps = [
        ":benchmark_config",
        ":driver_impl",
        ":qps_worker_impl",
    ],
)

grpc_cc_test(
    name = "secure_sync_unary_ping_pong_test",
    srcs = ["secure_sync_unary_ping_pong_test.cc"],
    deps = [
        ":benchmark_config",
        ":driver_impl",
        "//:grpc++",
    ],
)

grpc_cc_library(
    name = "usage_timer",
    srcs = ["usage_timer.cc"],
    hdrs = ["usage_timer.h"],
    deps = ["//:gpr"],
)

grpc_cc_binary(
    name = "qps_worker",
    srcs = ["worker.cc"],
    deps = [
        ":qps_worker_impl",
        "//:grpc++",
        "//test/core/util:gpr_test_util",
        "//test/core/util:grpc_test_util",
        "//test/cpp/util:test_config",
        "//test/cpp/util:test_util",
    ],
)
