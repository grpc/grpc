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

load("//bazel:grpc_build_system.bzl", "grpc_cc_test", "grpc_package")
load("//test/cpp/microbenchmarks:grpc_benchmark_config.bzl", "grpc_benchmark_args")

licenses(["notice"])

grpc_package(name = "test/core/promise/benchmark")

grpc_cc_test(
    name = "competition",
    srcs = [
        "competition.cc",
        "filter_stack.cc",
        "filter_stack.h",
    ],
    args = grpc_benchmark_args(),
    external_deps = [
        "benchmark",
        "absl/synchronization",
    ],
    tags = [
        "no_mac",
        "no_windows",
    ],
    uses_polling = False,
    deps = [
        "//:activity",
        "//:for_each",
        "//:join",
        "//:latch",
        "//:pipe",
        "//:seq",
        "//:try_join",
        "//:try_seq",
        "//test/core/promise:test_wakeup_schedulers",
        "//test/core/util:grpc_suppressions",
    ],
)
