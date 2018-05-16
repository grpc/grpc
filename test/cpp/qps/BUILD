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

grpc_package(name = "test/cpp/qps")

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
        "//:grpc++_core_stats",
        "//src/proto/grpc/testing:benchmark_service_proto",
        "//src/proto/grpc/testing:control_proto",
        "//src/proto/grpc/testing:payloads_proto",
        "//src/proto/grpc/testing:worker_service_proto",
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
        "//src/proto/grpc/testing:report_qps_scenario_service_proto",
        "//src/proto/grpc/testing:worker_service_proto",
        "//test/core/util:gpr_test_util",
        "//test/core/util:grpc_test_util",
        "//test/cpp/util:test_util",
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
    deps = ["//test/core/util:grpc_test_util"],
)

grpc_cc_test(
    name = "inproc_sync_unary_ping_pong_test",
    srcs = ["inproc_sync_unary_ping_pong_test.cc"],
    deps = [
        ":benchmark_config",
        ":driver_impl",
        "//:grpc++",
        "//test/cpp/util:test_config",
        "//test/cpp/util:test_util",
    ],
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
        "//test/cpp/util:test_config",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_test(
    name = "qps_interarrival_test",
    srcs = ["qps_interarrival_test.cc"],
    deps = [
        ":histogram",
        ":interarrival",
        "//test/cpp/util:test_config",
    ],
)

grpc_cc_binary(
    name = "qps_json_driver",
    srcs = ["qps_json_driver.cc"],
    deps = [
        ":benchmark_config",
        ":driver_impl",
        "//:grpc++",
        "//test/cpp/util:test_config",
        "//test/cpp/util:test_util",
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
        "//test/cpp/util:test_config",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_test(
    name = "secure_sync_unary_ping_pong_test",
    srcs = ["secure_sync_unary_ping_pong_test.cc"],
    deps = [
        ":benchmark_config",
        ":driver_impl",
        "//:grpc++",
        "//test/cpp/util:test_config",
        "//test/cpp/util:test_util",
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
