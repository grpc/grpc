# Copyright 2017 gRPC authors.
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

load("//bazel:grpc_build_system.bzl", "grpc_cc_library", "grpc_cc_test", "grpc_cc_binary")

licenses(["notice"])  # Apache v2

package(
    default_visibility = ["//visibility:public"],
    features = [
        "-layering_check",
        "-parse_headers",
    ],
)

grpc_cc_library(
    name = "gpr_test_util",
    srcs = [
        "memory_counters.c",
        "test_config.c",
    ],
    hdrs = [
        "memory_counters.h",
        "test_config.h",
    ],
    deps = ["//:gpr"],
)

grpc_cc_library(
    name = "grpc_test_util_base",
    srcs = [
        "debugger_macros.c",
        "grpc_profiler.c",
        "mock_endpoint.c",
        "parse_hexstring.c",
        "passthru_endpoint.c",
        "port.c",
        "port_server_client.c",
        "reconnect_server.c",
        "slice_splitter.c",
        "test_tcp_server.c",
        "trickle_endpoint.c",
    ],
    hdrs = [
        "debugger_macros.h",
        "grpc_profiler.h",
        "mock_endpoint.h",
        "parse_hexstring.h",
        "passthru_endpoint.h",
        "port.h",
        "port_server_client.h",
        "reconnect_server.h",
        "slice_splitter.h",
        "test_tcp_server.h",
        "trickle_endpoint.h",
    ],
    language = "C",
    deps = [
        ":gpr_test_util",
        "//:grpc_common",
    ],
)

grpc_cc_library(
    name = "grpc_test_util",
    srcs = [],
    hdrs = [],
    language = "C",
    deps = [
        ":grpc_test_util_base",
        "//:grpc",
    ],
)

grpc_cc_library(
    name = "grpc_test_util_unsecure",
    srcs = [],
    hdrs = [],
    language = "C",
    deps = [
        ":grpc_test_util_base",
        "//:grpc_unsecure",
    ],
)

grpc_cc_library(
    name = "one_corpus_entry_fuzzer",
    srcs = ["one_corpus_entry_fuzzer.c"],
    deps = [
        ":gpr_test_util",
        "//:grpc",
    ],
)

sh_library(
    name = "fuzzer_one_entry_runner",
    srcs = ["fuzzer_one_entry_runner.sh"],
)
