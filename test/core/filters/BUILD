# Copyright 2022 gRPC authors.
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
load("//test/core/util:grpc_fuzzer.bzl", "grpc_proto_fuzzer")

licenses(["notice"])

grpc_package(name = "test/core/filters")

grpc_cc_test(
    name = "client_auth_filter_test",
    srcs = ["client_auth_filter_test.cc"],
    external_deps = ["gtest"],
    language = "c++",
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:grpc",
        "//:grpc_security_base",
        "//src/core:channel_args",
        "//test/core/promise:test_context",
    ],
)

grpc_cc_test(
    name = "client_authority_filter_test",
    srcs = ["client_authority_filter_test.cc"],
    external_deps = ["gtest"],
    language = "c++",
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:grpc",
        "//src/core:grpc_client_authority_filter",
        "//test/core/promise:test_context",
    ],
)

grpc_proto_fuzzer(
    name = "filter_fuzzer",
    srcs = ["filter_fuzzer.cc"],
    corpus = "filter_fuzzer_corpus",
    language = "C++",
    proto = "filter_fuzzer.proto",
    tags = ["no_windows"],
    uses_polling = False,
    deps = [
        "//:gpr",
        "//:grpc",
        "//src/core:channel_args",
        "//src/core:closure",
        "//test/core/end2end:ssl_test_data",
        "//test/core/util:grpc_test_util",
    ],
)
