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

load("//bazel:grpc_build_system.bzl", "grpc_cc_library", "grpc_package")
load("//test/core/util:grpc_fuzzer.bzl", "grpc_fuzzer")

grpc_package(
    name = "test/core/transport/binder/end2end/fuzzers",
    features = [
        "layering_check",
    ],
)

licenses(["notice"])

grpc_cc_library(
    name = "fuzzer_utils",
    srcs = ["fuzzer_utils.cc"],
    hdrs = ["fuzzer_utils.h"],
    external_deps = [
        "absl/memory",
        "absl/status",
    ],
    tags = [
        "no_mac",
        "no_windows",
    ],
    deps = [
        "//:gpr",
        "//src/core/ext/transport/binder/wire_format:binder",
        "//src/core/ext/transport/binder/wire_format:wire_reader",
    ],
)

grpc_fuzzer(
    name = "client_fuzzer",
    srcs = ["client_fuzzer.cc"],
    corpus = "client_fuzzer_corpus",
    language = "C++",
    tags = [
        "no_mac",
        "no_windows",
    ],
    deps = [
        ":fuzzer_utils",
        "//:gpr",
        "//:gpr_base",
        "//:grpc++_base",
        "//:grpc_base",
        "//src/core/ext/transport/binder/transport:binder_transport",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_fuzzer(
    name = "server_fuzzer",
    srcs = ["server_fuzzer.cc"],
    corpus = "server_fuzzer_corpus",
    language = "C++",
    tags = [
        "no_mac",
        "no_windows",
    ],
    deps = [
        ":fuzzer_utils",
        "//:gpr",
        "//:gpr_base",
        "//:grpc++_base",
        "//:grpc_base",
        "//src/core/ext/transport/binder/transport:binder_transport",
        "//test/core/util:grpc_test_util",
    ],
)
