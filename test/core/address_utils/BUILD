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
load("//test/core/util:grpc_fuzzer.bzl", "grpc_fuzzer")

licenses(["notice"])

grpc_package(
    name = "test/core/address_utils",
    visibility = "private",
)

grpc_cc_test(
    name = "sockaddr_utils_test",
    srcs = ["sockaddr_utils_test.cc"],
    external_deps = [
        "gtest",
    ],
    language = "C++",
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "parse_address_test",
    srcs = ["parse_address_test.cc"],
    external_deps = [
        "absl/strings",
        "gtest",
    ],
    language = "C++",
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_fuzzer(
    name = "sockaddr_utils_fuzzer_test",
    srcs = ["sockaddr_utils_fuzzer_test.cc"],
    corpus = "sockaddr_uri_corpus",
    tags = ["no_windows"],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "parse_address_with_named_scope_id_test",
    srcs = ["parse_address_with_named_scope_id_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    tags = ["no_windows"],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)
