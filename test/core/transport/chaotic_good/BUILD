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

load("//bazel:grpc_build_system.bzl", "grpc_cc_library", "grpc_cc_proto_library", "grpc_cc_test", "grpc_internal_proto_library", "grpc_package")
load("//test/core/test_util:grpc_fuzzer.bzl", "grpc_fuzz_test")

licenses(["notice"])

grpc_package(
    name = "test/core/transport/chaotic_good",
    visibility = "tests",
)

grpc_cc_library(
    name = "transport_test_helper",
    testonly = 1,
    srcs = ["transport_test_helper.cc"],
    hdrs = ["transport_test_helper.h"],
    external_deps = ["gtest"],
    deps = [
        "//:iomgr_timer",
        "//src/core:chaotic_good_frame",
        "//src/core:memory_quota",
        "//src/core:resource_quota",
        "//test/core/event_engine/fuzzing_event_engine",
        "//test/core/event_engine/fuzzing_event_engine:fuzzing_event_engine_cc_proto",
    ],
)

grpc_cc_test(
    name = "tcp_frame_header_test",
    srcs = ["tcp_frame_header_test.cc"],
    external_deps = [
        "absl/status",
        "gtest",
    ],
    deps = ["//src/core:chaotic_good_tcp_frame_transport"],
)

grpc_fuzz_test(
    name = "tcp_frame_header_fuzzer",
    srcs = ["tcp_frame_header_fuzzer.cc"],
    external_deps = [
        "absl/status:statusor",
        "absl/strings",
        "gtest",
        "fuzztest",
        "fuzztest_main",
    ],
    tags = ["no_windows"],
    deps = [
        "//:grpc",
        "//src/core:chaotic_good_tcp_frame_transport",
    ],
)

grpc_fuzz_test(
    name = "serialize_little_endian_test",
    srcs = ["serialize_little_endian_test.cc"],
    external_deps = [
        "gtest",
        "fuzztest",
        "fuzztest_main",
    ],
    tags = ["no_windows"],
    deps = [
        "//:grpc",
        "//src/core:chaotic_good_serialize_little_endian",
    ],
)

grpc_fuzz_test(
    name = "tcp_frame_transport_test",
    srcs = ["tcp_frame_transport_test.cc"],
    external_deps = [
        "fuzztest",
        "fuzztest_main",
    ],
    tags = ["no_windows"],
    deps = [
        ":test_frame",
        "//src/core:chaotic_good_frame_transport",
        "//src/core:chaotic_good_tcp_frame_transport",
        "//src/core:inter_activity_latch",
        "//src/core:postmortem",
        "//test/core/event_engine/fuzzing_event_engine",
        "//test/core/event_engine/fuzzing_event_engine:fuzzing_event_engine_cc_proto",
    ],
)

grpc_fuzz_test(
    name = "frame_test",
    srcs = ["frame_test.cc"],
    external_deps = [
        "absl/log:check",
        "absl/random",
        "absl/status",
        "absl/status:statusor",
        "gtest",
        "fuzztest",
        "fuzztest_main",
    ],
    deps = [
        ":test_frame",
        "//:channel_create",
        "//src/core:chaotic_good_frame",
    ],
)

grpc_cc_library(
    name = "mock_frame_transport",
    testonly = 1,
    srcs = [
        "mock_frame_transport.cc",
    ],
    hdrs = [
        "mock_frame_transport.h",
    ],
    external_deps = [
        "gtest",
        "protobuf_headers",
    ],
    deps = [
        "//src/core:chaotic_good_frame_transport",
        "//src/core:inter_activity_latch",
        "//src/core:loop",
        "//src/core:try_seq",
        "//test/core/promise:poll_matcher",
    ],
)

grpc_cc_test(
    name = "client_transport_test",
    srcs = ["client_transport_test.cc"],
    external_deps = [
        "absl/functional:any_invocable",
        "absl/status:statusor",
        "absl/strings:str_format",
        "gtest",
    ],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "mock_frame_transport",
        "transport_test_helper",
        "//:grpc",
        "//:grpc_public_hdrs",
        "//src/core:arena",
        "//src/core:chaotic_good_client_transport",
        "//src/core:if",
        "//src/core:loop",
        "//src/core:seq",
        "//src/core:slice_buffer",
        "//test/core/transport/util:mock_promise_endpoint",
        "//test/core/transport/util:transport_test",
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
        "gtest",
    ],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "mock_frame_transport",
        "transport_test_helper",
        "//:grpc",
        "//:grpc_public_hdrs",
        "//:iomgr_timer",
        "//:ref_counted_ptr",
        "//src/core:arena",
        "//src/core:chaotic_good_frame_cc_proto",
        "//src/core:chaotic_good_server_transport",
        "//src/core:memory_quota",
        "//src/core:metadata_batch",
        "//src/core:resource_quota",
        "//src/core:seq",
        "//src/core:slice",
        "//src/core:slice_buffer",
        "//test/core/event_engine/fuzzing_event_engine",
        "//test/core/event_engine/fuzzing_event_engine:fuzzing_event_engine_cc_proto",
        "//test/core/transport/util:mock_promise_endpoint",
        "//test/core/transport/util:transport_test",
    ],
)

grpc_cc_test(
    name = "chaotic_good_server_test",
    srcs = ["chaotic_good_server_test.cc"],
    external_deps = [
        "absl/log:check",
        "absl/log:log",
        "absl/strings",
        "absl/time",
        "gtest",
    ],
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
        "//:uri",
        "//src/core:channel_args",
        "//src/core:chaotic_good",
        "//src/core:chaotic_good_connector",
        "//src/core:chaotic_good_server",
        "//src/core:notification",
        "//src/core:resource_quota",
        "//src/core:time",
        "//test/core/event_engine:event_engine_test_utils",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_fuzz_test(
    name = "control_endpoint_test",
    srcs = ["control_endpoint_test.cc"],
    external_deps = [
        "fuzztest",
        "fuzztest_main",
        "gtest",
    ],
    tags = [
        "no_windows",
    ],
    deps = [
        "//src/core:chaotic_good_control_endpoint",
        "//test/core/call/yodel:yodel_test",
        "//test/core/transport/util:mock_promise_endpoint",
    ],
)

grpc_fuzz_test(
    name = "data_endpoints_test",
    srcs = ["data_endpoints_test.cc"],
    external_deps = [
        "fuzztest",
        "fuzztest_main",
        "gtest",
    ],
    tags = [
        "no_windows",
    ],
    deps = [
        "//src/core:chaotic_good_data_endpoints",
        "//test/core/call/yodel:yodel_test",
        "//test/core/transport/util:mock_promise_endpoint",
    ],
)

grpc_fuzz_test(
    name = "config_test",
    srcs = ["config_test.cc"],
    external_deps = [
        "fuzztest",
        "fuzztest_main",
        "gtest",
    ],
    deps = [
        "//src/core:chaotic_good_config",
        "//src/core:chaotic_good_frame_cc_proto",
    ],
)

grpc_fuzz_test(
    name = "message_chunker_test",
    srcs = ["message_chunker_test.cc"],
    external_deps = [
        "fuzztest",
        "fuzztest_main",
        "gtest",
    ],
    deps = [
        "//src/core:chaotic_good_frame_cc_proto",
        "//src/core:chaotic_good_message_chunker",
        "//src/core:status_flag",
        "//test/core/promise:poll_matcher",
    ],
)

grpc_internal_proto_library(
    name = "test_frame_proto",
    srcs = ["test_frame.proto"],
    deps = [
        "//src/core:chaotic_good_frame_proto",
    ],
)

grpc_cc_proto_library(
    name = "test_frame_cc_proto",
    deps = [
        ":test_frame_proto",
    ],
)

grpc_cc_library(
    name = "test_frame",
    testonly = 1,
    srcs = ["test_frame.cc"],
    hdrs = ["test_frame.h"],
    external_deps = [
        "fuzztest",
    ],
    deps = [
        "test_frame_cc_proto",
        "//src/core:chaotic_good_frame",
    ],
)
