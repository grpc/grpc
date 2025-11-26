# Copyright 2012 gRPC authors.
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

load("//bazel:grpc_build_system.bzl", "grpc_cc_binary", "grpc_cc_library", "grpc_cc_test")

package(
    default_applicable_licenses = ["//:license"],
    features = [
        "layering_check",
        "parse_headers",
    ],
)

exports_files(
    glob(
        ["**"],
        exclude = [],
    ),
    visibility = ["//:__subpackages__"],
)

grpc_cc_binary(
    name = "gen_huffman_decompressor",
    srcs = ["gen_huffman_decompressor.cc"],
    external_deps = [
        "absl/strings",
        "absl/memory",
        "libcrypto",
    ],
    visibility = [
        "//test/cpp/microbenchmarks/huffman_geometries:__pkg__",
    ],
    deps = [
        "//src/core:env",
        "//src/core:huffsyms",
        "//src/core:match",
    ],
)

grpc_cc_binary(
    name = "gen_switch",
    srcs = ["gen_switch.cc"],
)

grpc_cc_library(
    name = "generate_trace_flags_lib",
    srcs = ["generate_trace_flags.cc"],
    hdrs = ["generate_trace_flags.h"],
    external_deps = [
        "absl/strings",
        "absl/strings:str_format",
        "absl/log",
        "absl/container:flat_hash_map",
        "@yaml-cpp//:yaml-cpp",
    ],
    deps = [
        "//:gpr",
    ],
)

grpc_cc_binary(
    name = "generate_trace_flags",
    srcs = ["generate_trace_flags_main.cc"],
    external_deps = [
        "absl/flags:flag",
        "absl/flags:parse",
        "absl/strings",
        "absl/log",
    ],
    visibility = [
        "//src/core:__pkg__",
    ],
    deps = [
        ":generate_trace_flags_lib",
    ],
)

grpc_cc_test(
    name = "generate_trace_flags_test",
    srcs = ["generate_trace_flags_test.cc"],
    data = ["generate_trace_flags_test_input.yaml"],
    external_deps = [
        "gtest",
        "gtest_main",
        "absl/strings",
    ],
    deps = [
        ":generate_trace_flags_lib",
        "//:gpr",
    ],
)
