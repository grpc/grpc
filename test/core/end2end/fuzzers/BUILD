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

load("//bazel:grpc_build_system.bzl", "grpc_cc_library", "grpc_cc_proto_library", "grpc_internal_proto_library", "grpc_package")
load("//test/core/test_util:grpc_fuzzer.bzl", "grpc_fuzz_test")

grpc_package(name = "test/core/end2end/fuzzers")

licenses(["notice"])

grpc_internal_proto_library(
    name = "api_fuzzer_proto",
    srcs = ["api_fuzzer.proto"],
    deps = [
        "//test/core/event_engine/fuzzing_event_engine:fuzzing_event_engine_proto",
        "//test/core/test_util:fuzz_config_vars_proto",
        "//test/core/test_util:fuzzing_channel_args_proto",
    ],
)

grpc_cc_proto_library(
    name = "api_fuzzer_cc_proto",
    deps = ["api_fuzzer_proto"],
)

grpc_cc_library(
    name = "fuzzing_common",
    testonly = True,
    srcs = ["fuzzing_common.cc"],
    hdrs = ["fuzzing_common.h"],
    external_deps = [
        "absl/log:check",
        "absl/log:log",
        "absl/strings",
    ],
    deps = [
        "api_fuzzer_cc_proto",
        "//:gpr",
        "//:grpc",
        "//test/core/event_engine/fuzzing_event_engine",
    ],
)

grpc_fuzz_test(
    name = "api_fuzzer",
    srcs = ["api_fuzzer.cc"],
    external_deps = [
        "absl/log:check",
        "fuzztest",
        "fuzztest_main",
    ],
    tags = [
        "no_mac",
        "no_windows",
    ],
    deps = [
        "api_fuzzer_cc_proto",
        "fuzzing_common",
        "//:gpr",
        "//:grpc",
        "//src/core:channel_args",
        "//src/core:closure",
        "//src/core:slice",
        "//test/core/end2end:ssl_test_data",
        "//test/core/event_engine/fuzzing_event_engine",
        "//test/core/test_util:fuzz_config_vars",
        "//test/core/test_util:fuzz_config_vars_helpers",
        "//test/core/test_util:fuzzing_channel_args",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
    ],
)

grpc_internal_proto_library(
    name = "fuzzer_input_proto",
    srcs = ["fuzzer_input.proto"],
    deps = [
        "api_fuzzer_proto",
        "//src/core:chaotic_good_frame_proto",
        "//test/core/event_engine/fuzzing_event_engine:fuzzing_event_engine_proto",
        "//test/core/test_util:fuzz_config_vars_proto",
        "//test/core/test_util:fuzzing_channel_args_proto",
    ],
)

grpc_cc_proto_library(
    name = "fuzzer_input_cc_proto",
    deps = ["fuzzer_input_proto"],
)

grpc_cc_library(
    name = "network_input",
    testonly = True,
    srcs = ["network_input.cc"],
    hdrs = ["network_input.h"],
    external_deps = ["absl/log:log"],
    deps = [
        "fuzzer_input_cc_proto",
        "//:chttp2_frame",
        "//src/core:chaotic_good_frame_header",
        "//src/core:chaotic_good_tcp_frame_transport",
        "//test/core/event_engine/fuzzing_event_engine",
        "//test/core/test_util:fuzzing_channel_args",
        "//test/core/test_util:grpc_test_util_base",
    ],
)

grpc_fuzz_test(
    name = "client_fuzzer",
    srcs = ["client_fuzzer.cc"],
    external_deps = [
        "absl/log:check",
        "fuzztest",
        "fuzztest_main",
    ],
    tags = ["no_windows"],
    deps = [
        "fuzzer_input_cc_proto",
        "fuzzing_common",
        "network_input",
        "//:gpr",
        "//:grpc",
        "//src/core:channel_args",
        "//test/core/event_engine/fuzzing_event_engine",
        "//test/core/test_util:fuzz_config_vars",
        "//test/core/test_util:fuzz_config_vars_helpers",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
    ],
)

grpc_fuzz_test(
    name = "server_fuzzer",
    srcs = ["server_fuzzer.cc"],
    external_deps = [
        "absl/log:check",
        "fuzztest",
        "fuzztest_main",
        "gtest",
    ],
    deps = [
        "fuzzer_input_cc_proto",
        "fuzzing_common",
        "network_input",
        "//:gpr",
        "//:grpc",
        "//src/core:channel_args",
        "//src/core:chaotic_good_server",
        "//test/core/event_engine/fuzzing_event_engine",
        "//test/core/test_util:fuzz_config_vars",
        "//test/core/test_util:fuzz_config_vars_helpers",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
    ],
)

grpc_fuzz_test(
    name = "connector_fuzzer",
    srcs = ["connector_fuzzer.cc"],
    external_deps = [
        "absl/log:check",
        "gtest",
        "fuzztest",
        "fuzztest_main",
    ],
    deps = [
        "fuzzer_input_cc_proto",
        "fuzzing_common",
        "network_input",
        "//:gpr",
        "//:grpc",
        "//src/core:channel_args",
        "//test/core/event_engine/fuzzing_event_engine",
        "//test/core/test_util:fuzz_config_vars",
        "//test/core/test_util:fuzz_config_vars_helpers",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
    ],
)
