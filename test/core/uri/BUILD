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

load("//bazel:grpc_build_system.bzl", "grpc_cc_test", "grpc_package")

grpc_package(name = "test/core/client_channel")

licenses(["notice"])

load("//test/core/util:grpc_fuzzer.bzl", "grpc_fuzzer")

grpc_fuzzer(
    name = "uri_fuzzer_test",
    srcs = ["uri_fuzzer_test.cc"],
    corpus = "uri_corpus",
    language = "C++",
    tags = ["no_windows"],
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "uri_parser_test",
    srcs = ["uri_parser_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    deps = [
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)
