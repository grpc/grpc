# Copyright 2018 gRPC authors.
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
load("//bazel:cc_grpc_library.bzl", "cc_grpc_library")

licenses(["notice"])  # Apache v2

grpc_package(name = "test/core/ext/census")

cc_grpc_library(
  name = "echo_proto",
  srcs = ["echo.proto"],
  deps = [],
  proto_only = False,
  well_known_protos = False,
  generate_mocks = False,
  use_external = True,
)

grpc_cc_library(
    name = "grpc_opencensus_plugin_test_lib",
    testonly = True,
    srcs = [
        "stats_plugin_end2end_test.cc",
    ],
    language = "C++",
    external_deps = [
        "gtest",
        "gmock",
    ],
    deps = [
        ":echo_proto",
        "//:grpc++",
        "//:grpc_opencensus_plugin",
        "//test/core/util:gpr_test_util",
        "//test/core/util:grpc_test_util",
        "//test/cpp/util:test_util",
        "//test/cpp/util:test_config",
    ],
)

grpc_cc_test(
  name = "grpc_opencensus_plugin_test",
  deps = [
    ":grpc_opencensus_plugin_test_lib",
  ],
)

grpc_cc_test(
    name = "grpc_plugin_benchmark",
    srcs = ["grpc_plugin_benchmark.cc"],
    language = "C++",
    deps = [
        ":echo_proto",
        "//:grpc_opencensus_plugin",
    ],
    external_deps = [
        "benchmark",
    ],
)
