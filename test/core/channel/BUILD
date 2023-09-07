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

grpc_package(name = "test/core/channel")

licenses(["notice"])

grpc_cc_test(
    name = "call_tracer_test",
    srcs = ["call_tracer_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "channel_args_test",
    srcs = ["channel_args_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:gpr",
        "//:grpc",
        "//src/core:channel_args",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "channel_stack_test",
    srcs = ["channel_stack_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:gpr",
        "//:grpc",
        "//src/core:channel_args",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "channel_stack_builder_test",
    srcs = ["channel_stack_builder_test.cc"],
    external_deps = [
        "gtest",
    ],
    language = "C++",
    deps = [
        "//:channel_stack_builder",
        "//:gpr",
        "//:grpc",
        "//src/core:closure",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "minimal_stack_is_minimal_test",
    srcs = ["minimal_stack_is_minimal_test.cc"],
    external_deps = [
        "gtest",
    ],
    language = "C++",
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:gpr",
        "//:grpc",
        "//src/core:channel_args",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "channel_trace_test",
    srcs = ["channel_trace_test.cc"],
    external_deps = [
        "gtest",
    ],
    language = "C++",
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//src/core:channel_args",
        "//test/core/util:grpc_test_util",
        "//test/cpp/util:channel_trace_proto_helper",
    ],
)

grpc_cc_test(
    name = "channelz_test",
    srcs = ["channelz_test.cc"],
    external_deps = [
        "gtest",
    ],
    language = "C++",
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//src/core:channel_args",
"//test/core/event_engine:event_engine_test_utils",        "//test/core/util:grpc_test_util",
        "//test/cpp/util:channel_trace_proto_helper",
    ],
)

grpc_cc_test(
    name = "channelz_registry_test",
    srcs = ["channelz_registry_test.cc"],
    external_deps = [
        "gtest",
    ],
    language = "C++",
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "status_util_test",
    srcs = ["status_util_test.cc"],
    external_deps = [
        "gtest",
    ],
    language = "C++",
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "call_finalization_test",
    srcs = ["call_finalization_test.cc"],
    external_deps = [
        "gtest",
    ],
    language = "C++",
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/promise:test_context",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "server_call_tracer_factory_test",
    srcs = ["server_call_tracer_factory_test.cc"],
    external_deps = [
        "gtest",
    ],
    language = "C++",
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)
