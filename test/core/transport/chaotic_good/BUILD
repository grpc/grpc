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

load("//bazel:grpc_build_system.bzl", "grpc_cc_library", "grpc_cc_test", "grpc_package")
load("//test/core/util:grpc_fuzzer.bzl", "grpc_fuzzer", "grpc_proto_fuzzer")

licenses(["notice"])

grpc_package(
    name = "test/core/transport/chaotic_good",
    visibility = "tests",
)

grpc_cc_library(
    name = "mock_promise_endpoint",
    testonly = 1,
    srcs = ["mock_promise_endpoint.cc"],
    hdrs = ["mock_promise_endpoint.h"],
    external_deps = ["gtest"],
    deps = [
        "//:grpc",
        "//src/core:grpc_promise_endpoint",
    ],
)

grpc_cc_library(
    name = "transport_test",
    testonly = 1,
    srcs = ["transport_test.cc"],
    hdrs = ["transport_test.h"],
    external_deps = ["gtest"],
    deps = [
        "//:iomgr_timer",
        "//src/core:chaotic_good_frame",
        "//src/core:memory_quota",
        "//src/core:resource_quota",
        "//test/core/event_engine/fuzzing_event_engine",
        "//test/core/event_engine/fuzzing_event_engine:fuzzing_event_engine_proto",
    ],
)

grpc_cc_test(
    name = "frame_header_test",
    srcs = ["frame_header_test.cc"],
    external_deps = [
        "absl/status",
        "gtest",
    ],
    deps = ["//src/core:chaotic_good_frame_header"],
)

grpc_fuzzer(
    name = "frame_header_fuzzer",
    srcs = ["frame_header_fuzzer.cc"],
    corpus = "frame_header_fuzzer_corpus",
    external_deps = ["absl/status:statusor"],
    language = "C++",
    tags = ["no_windows"],
    deps = [
        "//:grpc",
        "//src/core:chaotic_good_frame_header",
    ],
)

grpc_cc_test(
    name = "frame_test",
    srcs = ["frame_test.cc"],
    external_deps = [
        "absl/random",
        "absl/status",
        "absl/status:statusor",
        "gtest",
    ],
    deps = [
        "//:channel_create",
        "//src/core:chaotic_good_frame",
    ],
)

grpc_proto_fuzzer(
    name = "frame_fuzzer",
    srcs = ["frame_fuzzer.cc"],
    corpus = "frame_fuzzer_corpus",
    external_deps = [
        "absl/random:bit_gen_ref",
        "absl/status:statusor",
    ],
    language = "C++",
    proto = "frame_fuzzer.proto",
    tags = ["no_windows"],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:exec_ctx",
        "//:gpr",
        "//:hpack_encoder",
        "//:hpack_parser",
        "//:ref_counted_ptr",
        "//src/core:arena",
        "//src/core:chaotic_good_frame",
        "//src/core:chaotic_good_frame_header",
        "//src/core:event_engine_memory_allocator",
        "//src/core:memory_quota",
        "//src/core:resource_quota",
        "//src/core:slice",
        "//src/core:slice_buffer",
        "//test/core/promise:test_context",
    ],
)

grpc_cc_test(
    name = "client_transport_test",
    srcs = ["client_transport_test.cc"],
    external_deps = [
        "absl/functional:any_invocable",
        "absl/status:statusor",
        "absl/strings:str_format",
        "absl/types:optional",
        "gtest",
    ],
    language = "C++",
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "mock_promise_endpoint",
        "transport_test",
        "//:grpc",
        "//:grpc_public_hdrs",
        "//src/core:arena",
        "//src/core:chaotic_good_client_transport",
        "//src/core:if",
        "//src/core:loop",
        "//src/core:seq",
        "//src/core:slice_buffer",
    ],
)

grpc_cc_test(
    name = "client_transport_error_test",
    srcs = ["client_transport_error_test.cc"],
    external_deps = [
        "absl/functional:any_invocable",
        "absl/status",
        "absl/status:statusor",
        "absl/strings:str_format",
        "absl/types:optional",
        "gtest",
    ],
    language = "C++",
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:grpc_public_hdrs",
        "//:grpc_unsecure",
        "//:iomgr_timer",
        "//:ref_counted_ptr",
        "//src/core:activity",
        "//src/core:arena",
        "//src/core:chaotic_good_client_transport",
        "//src/core:event_engine_wakeup_scheduler",
        "//src/core:grpc_promise_endpoint",
        "//src/core:if",
        "//src/core:join",
        "//src/core:loop",
        "//src/core:memory_quota",
        "//src/core:pipe",
        "//src/core:resource_quota",
        "//src/core:seq",
        "//src/core:slice",
        "//src/core:slice_buffer",
        "//test/core/event_engine/fuzzing_event_engine",
        "//test/core/event_engine/fuzzing_event_engine:fuzzing_event_engine_proto",
    ],
)

grpc_cc_test(
    name = "server_transport_test",
    srcs = ["server_transport_test.cc"],
    external_deps = [
        "absl/functional:any_invocable",
        "absl/status",
        "absl/status:statusor",
        "absl/strings:str_format",
        "absl/types:optional",
        "gtest",
    ],
    language = "C++",
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "mock_promise_endpoint",
        "transport_test",
        "//:grpc",
        "//:grpc_public_hdrs",
        "//:iomgr_timer",
        "//:ref_counted_ptr",
        "//src/core:arena",
        "//src/core:chaotic_good_server_transport",
        "//src/core:memory_quota",
        "//src/core:resource_quota",
        "//src/core:seq",
        "//src/core:slice",
        "//src/core:slice_buffer",
        "//test/core/event_engine/fuzzing_event_engine",
        "//test/core/event_engine/fuzzing_event_engine:fuzzing_event_engine_proto",
    ],
)

grpc_cc_test(
    name = "chaotic_good_server_test",
    srcs = ["chaotic_good_server_test.cc"],
    external_deps = [
        "absl/strings",
        "absl/time",
        "gtest",
    ],
    language = "C++",
    tags = [
        "no_windows",
    ],
    uses_event_engine = True,
    uses_polling = False,
    deps = [
        "//:grpc",
        "//:grpc++",
        "//:grpc_public_hdrs",
        "//:parse_address",
        "//:uri_parser",
        "//src/core:channel_args",
        "//src/core:chaotic_good_connector",
        "//src/core:chaotic_good_server",
        "//src/core:notification",
        "//src/core:resource_quota",
        "//src/core:time",
        "//test/core/event_engine:event_engine_test_utils",
        "//test/core/util:grpc_test_util",
    ],
)
