# Copyright 2022 gRPC authors.
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

grpc_package(name = "test/core/event_engine/test_suite")

grpc_cc_library(
    name = "timer",
    testonly = True,
    srcs = ["timer_test.cc"],
    deps = [":conformance_test_base_lib"],
    alwayslink = 1,
)

grpc_cc_library(
    name = "dns",
    testonly = True,
    srcs = ["dns_test.cc"],
    deps = [":conformance_test_base_lib"],
    alwayslink = 1,
)

grpc_cc_library(
    name = "client",
    testonly = True,
    srcs = ["client_test.cc"],
    deps = [":conformance_test_base_lib"],
    alwayslink = 1,
)

grpc_cc_library(
    name = "server",
    testonly = True,
    srcs = ["server_test.cc"],
    deps = [":conformance_test_base_lib"],
    alwayslink = 1,
)

grpc_cc_library(
    name = "complete",
    testonly = 1,
    deps = [
        ":event_engine_test_suite@client",
        ":event_engine_test_suite@dns",
        ":event_engine_test_suite@server",
        ":event_engine_test_suite@timer",
    ],
    alwayslink = 1,
)

# -- Internal targets --

grpc_cc_library(
    name = "conformance_test_base_lib",
    testonly = True,
    srcs = ["event_engine_test.cc"],
    hdrs = ["event_engine_test.h"],
    external_deps = ["gtest"],
    deps = [
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)
