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

load("//bazel:grpc_build_system.bzl", "grpc_cc_library", "grpc_package", "grpc_proto_library")
load("//test/core/util:grpc_fuzzer.bzl", "grpc_proto_fuzzer")

grpc_package(name = "test/core/end2end/fuzzers")

licenses(["notice"])

grpc_proto_library(
    name = "api_fuzzer_proto",
    srcs = ["api_fuzzer.proto"],
    has_services = False,
    deps = [
        "//test/core/event_engine/fuzzing_event_engine:fuzzing_event_engine_proto",
        "//test/core/util:fuzz_config_vars_proto",
        "//test/core/util:fuzzing_channel_args_proto",
    ],
)

grpc_cc_library(
    name = "fuzzing_common",
    srcs = ["fuzzing_common.cc"],
    hdrs = ["fuzzing_common.h"],
    external_deps = ["absl/strings"],
    deps = [
        "api_fuzzer_proto",
        "//:gpr",
        "//:grpc",
        "//test/core/event_engine/fuzzing_event_engine",
    ],
)

grpc_proto_fuzzer(
    name = "api_fuzzer",
    srcs = ["api_fuzzer.cc"],
    corpus = "api_fuzzer_corpus",
    language = "C++",
    proto = None,
    tags = [
        "no_mac",
        "no_windows",
    ],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "api_fuzzer_proto",
        "fuzzing_common",
        "//:gpr",
        "//:grpc",
        "//src/core:channel_args",
        "//src/core:closure",
        "//src/core:slice",
        "//test/core/end2end:ssl_test_data",
        "//test/core/event_engine/fuzzing_event_engine",
        "//test/core/util:fuzz_config_vars",
        "//test/core/util:fuzzing_channel_args",
        "//test/core/util:grpc_test_util",
        "//test/core/util:grpc_test_util_base",
    ],
)

grpc_proto_library(
    name = "fuzzer_input_proto",
    srcs = ["fuzzer_input.proto"],
    has_services = False,
    deps = [
        "api_fuzzer_proto",
        "//test/core/event_engine/fuzzing_event_engine:fuzzing_event_engine_proto",
        "//test/core/util:fuzz_config_vars_proto",
        "//test/core/util:fuzzing_channel_args_proto",
    ],
)

grpc_cc_library(
    name = "network_input",
    srcs = ["network_input.cc"],
    hdrs = ["network_input.h"],
    deps = [
        "fuzzer_input_proto",
        "//:chttp2_frame",
        "//src/core:chaotic_good_frame_header",
        "//test/core/event_engine/fuzzing_event_engine",
        "//test/core/util:fuzzing_channel_args",
        "//test/core/util:grpc_test_util_base",
    ],
)

grpc_proto_fuzzer(
    name = "client_fuzzer",
    srcs = ["client_fuzzer.cc"],
    corpus = "client_fuzzer_corpus",
    language = "C++",
    proto = None,
    tags = ["no_windows"],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "fuzzer_input_proto",
        "fuzzing_common",
        "network_input",
        "//:gpr",
        "//:grpc",
        "//src/core:channel_args",
        "//test/core/event_engine/fuzzing_event_engine",
        "//test/core/util:fuzz_config_vars",
        "//test/core/util:grpc_test_util",
        "//test/core/util:grpc_test_util_base",
    ],
)

grpc_cc_library(
    name = "server_fuzzer",
    srcs = ["server_fuzzer.cc"],
    hdrs = ["server_fuzzer.h"],
    deps = [
        "fuzzer_input_proto",
        "fuzzing_common",
        "network_input",
        "//:gpr",
        "//:grpc",
        "//src/core:channel_args",
        "//test/core/event_engine/fuzzing_event_engine",
        "//test/core/util:fuzz_config_vars",
        "//test/core/util:grpc_test_util",
        "//test/core/util:grpc_test_util_base",
    ],
)

grpc_proto_fuzzer(
    name = "server_fuzzer_chttp2",
    srcs = ["server_fuzzer_chttp2.cc"],
    corpus = "server_fuzzer_chttp2_corpus",
    language = "C++",
    proto = None,
    tags = [
        "no_mac",
        "no_windows",
    ],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        ":server_fuzzer",
        "//:grpc",
    ],
)

grpc_proto_fuzzer(
    name = "server_fuzzer_chaotic_good",
    srcs = ["server_fuzzer_chaotic_good.cc"],
    corpus = "server_fuzzer_chaotic_good_corpus",
    language = "C++",
    proto = None,
    tags = [
        "no_mac",
        "no_windows",
    ],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        ":server_fuzzer",
        "//src/core:chaotic_good_server",
    ],
)
