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

load("//bazel:grpc_build_system.bzl", "grpc_cc_binary", "grpc_cc_library", "grpc_cc_test", "grpc_package")

grpc_package(name = "test/core/memory_usage")

licenses(["notice"])

grpc_cc_library(
    name = "memstats",
    srcs = [
        "memstats.cc",
        "memstats.h",
    ],
    hdrs = ["memstats.h"],
    external_deps = [
        "absl/types:optional",
    ],
    tags = [
        "bazel_only",
        "no_mac",
        "no_windows",
    ],
    deps = [
        "//:gpr",
    ],
)

grpc_cc_binary(
    name = "memory_usage_client",
    srcs = ["client.cc"],
    external_deps = [
        "absl/flags:flag",
        "absl/flags:parse",
    ],
    tags = [
        "bazel_only",
        "no_mac",
        "no_windows",
    ],
    deps = [
        ":memstats",
        "//:gpr",
        "//:grpc",
        "//src/core:channel_args",
        "//test/core/util:grpc_test_util",
        "//test/core/util:grpc_test_util_base",
    ],
)

grpc_cc_binary(
    name = "memory_usage_server",
    srcs = ["server.cc"],
    external_deps = [
        "absl/flags:flag",
        "absl/flags:parse",
    ],
    tags = [
        "bazel_only",
        "no_mac",
        "no_windows",
    ],
    deps = [
        ":memstats",
        "//:gpr",
        "//:grpc",
        "//src/core:channel_args",
        "//test/core/end2end:ssl_test_data",
        "//test/core/util:grpc_test_util",
        "//test/core/util:grpc_test_util_base",
    ],
)

grpc_cc_binary(
    name = "memory_usage_callback_client",
    srcs = ["callback_client.cc"],
    external_deps = [
        "absl/flags:flag",
        "absl/flags:parse",
        "absl/time",
    ],
    tags = [
        "bazel_only",
        "no_mac",
        "no_windows",
    ],
    deps = [
        ":memstats",
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//src/proto/grpc/testing:benchmark_service_proto",
        "//test/core/end2end:ssl_test_data",
        "//test/core/util:grpc_test_util",
        "//test/core/util:grpc_test_util_base",
    ],
)

grpc_cc_binary(
    name = "memory_usage_callback_server",
    srcs = ["callback_server.cc"],
    external_deps = [
        "absl/flags:flag",
        "absl/flags:parse",
    ],
    tags = [
        "bazel_only",
        "no_mac",
        "no_windows",
    ],
    deps = [
        ":memstats",
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//src/proto/grpc/testing:benchmark_service_proto",
        "//test/core/end2end:ssl_test_data",
        "//test/core/util:grpc_test_util",
        "//test/core/util:grpc_test_util_base",
    ],
)

MEMORY_USAGE_DATA = [
    ":memory_usage_callback_client",
    ":memory_usage_callback_server",
    ":memory_usage_client",
    ":memory_usage_server",
]

MEMORY_USAGE_TAGS = [
    "bazel_only",
    "no_mac",
    "no_windows",
]

grpc_cc_test(
    name = "memory_usage_test",
    srcs = ["memory_usage_test.cc"],
    data = MEMORY_USAGE_DATA,
    external_deps = [
        "absl/algorithm:container",
        "absl/flags:flag",
        "absl/flags:parse",
    ],
    language = "C++",
    tags = MEMORY_USAGE_TAGS,
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:grpc_test_util",
        "//test/core/util:grpc_test_util_base",
    ],
)

grpc_cc_binary(
    name = "memory_usage",
    srcs = ["memory_usage_test.cc"],
    data = MEMORY_USAGE_DATA,
    external_deps = [
        "absl/algorithm:container",
        "absl/flags:flag",
        "absl/flags:parse",
    ],
    language = "C++",
    tags = MEMORY_USAGE_TAGS,
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:grpc_test_util",
        "//test/core/util:grpc_test_util_base",
    ],
)
