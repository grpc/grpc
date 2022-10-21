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

load("//bazel:grpc_build_system.bzl", "grpc_cc_test", "grpc_package")

licenses(["notice"])

grpc_package(name = "test/cpp/server/load_reporter")

grpc_cc_test(
    name = "lb_load_data_store_test",
    srcs = ["load_data_store_test.cc"],
    external_deps = [
        "gtest",
    ],
    deps = [
        "//:lb_load_data_store",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "lb_load_reporter_test",
    srcs = ["load_reporter_test.cc"],
    external_deps = [
        "gtest",
        "opencensus-stats-test",
    ],
    tags = ["no_windows"],
    deps = [
        "//:gpr",
        "//:grpc",
        "//:lb_load_reporter",
        "//src/core:lb_server_load_reporting_filter",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "lb_get_cpu_stats_test",
    srcs = ["get_cpu_stats_test.cc"],
    external_deps = [
        "gtest",
    ],
    deps = [
        "//:gpr",
        "//:grpc",
        "//:lb_get_cpu_stats",
        "//test/core/util:grpc_test_util",
    ],
)
