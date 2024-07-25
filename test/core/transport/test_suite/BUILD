# Copyright 2023 gRPC authors.
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

load(
    "//bazel:grpc_build_system.bzl",
    "grpc_cc_library",
    "grpc_package",
)
load("grpc_transport_test.bzl", "grpc_transport_test")

grpc_package(name = "test/core/transport/test_suite")

grpc_cc_library(
    name = "inproc_fixture",
    testonly = 1,
    srcs = ["inproc_fixture.cc"],
    deps = [
        "test",
        "//src/core:grpc_transport_inproc",
    ],
    alwayslink = 1,
)

grpc_cc_library(
    name = "chaotic_good_fixture",
    testonly = 1,
    srcs = ["chaotic_good_fixture.cc"],
    external_deps = [
        "absl/log:check",
        "gtest",
    ],
    deps = [
        "test",
        "//src/core:chaotic_good_client_transport",
        "//src/core:chaotic_good_server_transport",
        "//src/core:event_engine_memory_allocator_factory",
        "//src/core:event_engine_tcp_socket_utils",
        "//src/core:grpc_promise_endpoint",
        "//src/core:resource_quota",
    ],
    alwayslink = 1,
)

grpc_cc_library(
    name = "test",
    testonly = 1,
    srcs = ["transport_test.cc"],
    hdrs = ["transport_test.h"],
    external_deps = [
        "absl/log:log",
        "absl/random",
        "absl/random:bit_gen_ref",
        "gtest",
    ],
    deps = [
        "//test/core/call/yodel:yodel_test",
    ],
)

grpc_cc_library(
    name = "call_content",
    testonly = 1,
    srcs = ["call_content.cc"],
    external_deps = ["gtest"],
    deps = ["test"],
    alwayslink = 1,
)

grpc_cc_library(
    name = "call_shapes",
    testonly = 1,
    srcs = ["call_shapes.cc"],
    deps = ["test"],
    alwayslink = 1,
)

grpc_cc_library(
    name = "no_op",
    testonly = 1,
    srcs = ["no_op.cc"],
    deps = ["test"],
    alwayslink = 1,
)

grpc_cc_library(
    name = "stress",
    testonly = 1,
    srcs = ["stress.cc"],
    external_deps = ["absl/random"],
    deps = ["test"],
    alwayslink = 1,
)

grpc_transport_test(
    name = "inproc",
    deps = [
        ":call_content",
        ":call_shapes",
        ":inproc_fixture",
        ":no_op",
        ":stress",
    ],
)

grpc_transport_test(
    name = "chaotic_good",
    deps = [
        ":call_content",
        ":call_shapes",
        ":chaotic_good_fixture",
        ":no_op",
        ":stress",
    ],
)
