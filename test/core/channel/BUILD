# Copyright 2016 gRPC authors.
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
load("//test/core/test_util:grpc_fuzzer.bzl", "grpc_fuzz_test")

grpc_package(name = "test/core/channel")

licenses(["notice"])

grpc_cc_test(
    name = "channel_args_test",
    srcs = ["channel_args_test.cc"],
    external_deps = [
        "absl/log:check",
        "absl/log:log",
        "gtest",
    ],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:gpr",
        "//:grpc",
        "//src/core:channel_args",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "channel_stack_test",
    srcs = ["channel_stack_test.cc"],
    external_deps = ["gtest"],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:gpr",
        "//:grpc",
        "//src/core:channel_args",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "channel_stack_builder_test",
    srcs = ["channel_stack_builder_test.cc"],
    external_deps = [
        "gtest",
    ],
    deps = [
        "//:channel_stack_builder",
        "//:gpr",
        "//:grpc",
        "//src/core:closure",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "minimal_stack_is_minimal_test",
    srcs = ["minimal_stack_is_minimal_test.cc"],
    external_deps = [
        "absl/log:check",
        "gtest",
    ],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:gpr",
        "//:grpc",
        "//src/core:channel_args",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "status_util_test",
    srcs = ["status_util_test.cc"],
    external_deps = [
        "gtest",
    ],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:grpc",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "call_finalization_test",
    srcs = ["call_finalization_test.cc"],
    external_deps = [
        "gtest",
    ],
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/promise:test_context",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "server_call_tracer_factory_test",
    srcs = ["server_call_tracer_factory_test.cc"],
    external_deps = [
        "gtest",
    ],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_fuzz_test(
    name = "union_with_test",
    srcs = ["union_with_test.cc"],
    external_deps = [
        "fuzztest",
        "fuzztest_main",
        "gtest",
    ],
    deps = ["//src/core:channel_args"],
)
