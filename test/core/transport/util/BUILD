# Copyright 2024 gRPC authors.
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

licenses(["notice"])

grpc_package(
    name = "test/core/transport/util",
    visibility = "tests",
)

grpc_cc_library(
    name = "mock_promise_endpoint",
    testonly = 1,
    srcs = ["mock_promise_endpoint.cc"],
    hdrs = ["mock_promise_endpoint.h"],
    external_deps = [
        "absl/log:log",
        "gtest",
    ],
    deps = [
        "//:grpc",
        "//src/core:grpc_promise_endpoint",
    ],
)

grpc_cc_library(
    name = "transport_test",
    testonly = 1,
    hdrs = ["transport_test.h"],
    external_deps = [
        "absl/log:log",
        "gtest",
    ],
    deps = [
        "//:iomgr_timer",
        "//src/core:memory_quota",
        "//src/core:resource_quota",
        "//test/core/event_engine/fuzzing_event_engine",
        "//test/core/event_engine/fuzzing_event_engine:fuzzing_event_engine_cc_proto",
    ],
)
