# Copyright 2021 gRPC authors.
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

load("//bazel:grpc_build_system.bzl", "grpc_package")
load("//test/cpp/microbenchmarks:grpc_benchmark_config.bzl", "HISTORY", "grpc_cc_benchmark")

licenses(["notice"])

grpc_package(
    name = "test/core/transport/benchmarks",
)

grpc_cc_benchmark(
    name = "bm_chaotic_good",
    srcs = ["bm_chaotic_good.cc"],
    monitoring = HISTORY,
    deps = [
        "//:grpc",
        "//src/core:chaotic_good_client_transport",
        "//src/core:chaotic_good_server_transport",
        "//src/core:default_event_engine",
        "//test/core/call:call_spine_benchmarks",
        "//test/core/test_util:passthrough_endpoint",
    ],
)

grpc_cc_benchmark(
    name = "bm_inproc",
    srcs = ["bm_inproc.cc"],
    monitoring = HISTORY,
    deps = [
        "//:grpc",
        "//src/core:chaotic_good_client_transport",
        "//src/core:chaotic_good_server_transport",
        "//src/core:default_event_engine",
        "//test/core/call:call_spine_benchmarks",
        "//test/core/test_util:passthrough_endpoint",
    ],
)
