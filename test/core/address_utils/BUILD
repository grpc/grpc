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
load("//test/core/test_util:grpc_fuzzer.bzl", "grpc_fuzz_test")

licenses(["notice"])

grpc_package(
    name = "test/core/address_utils",
    visibility = "private",
)

grpc_cc_test(
    name = "sockaddr_utils_test",
    srcs = ["sockaddr_utils_test.cc"],
    external_deps = [
        "absl/log:check",
        "gtest",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
    ],
    deps = [
        "//:gpr",
        "//:grpc",
        "//:parse_address",
        "//:sockaddr_utils",
        "//src/core:grpc_sockaddr",
        "//src/core:iomgr_port",
        "//src/core:resolved_address",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "parse_address_test",
    srcs = ["parse_address_test.cc"],
    external_deps = [
        "absl/log:log",
        "absl/strings",
        "gtest",
        "absl/status",
    ],
    deps = [
        "//:exec_ctx",
        "//:gpr",
        "//:grpc",
        "//:parse_address",
        "//src/core:grpc_sockaddr",
        "//src/core:iomgr_port",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_fuzz_test(
    name = "sockaddr_utils_fuzzer_test",
    srcs = ["sockaddr_utils_fuzzer_test.cc"],
    external_deps = [
        "absl/log:check",
        "fuzztest",
        "fuzztest_main",
        "absl/status:statusor",
    ],
    tags = ["no_windows"],
    deps = [
        "//:grpc",
        "//:iomgr",
        "//:sockaddr_utils",
        "//:uri",
        "//src/core:resolved_address",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "parse_address_with_named_scope_id_test",
    srcs = ["parse_address_with_named_scope_id_test.cc"],
    external_deps = [
        "absl/log:log",
        "gtest",
    ],
    tags = [
        # Invalid interface name: 'lo'.
        "grpc:fails-internally",
        "no_windows",
    ],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "uds_socket_test",
    srcs = ["uds_socket_test.cc"],
    external_deps = [
        "absl/log:log",
        "gtest",
        "absl/status",
    ],
    tags = ["no_windows"],
    deps = [
        "//:gpr",
        "//:grpc",
        "//:sockaddr_utils",
        "//src/core:iomgr_port",
        "//src/core:resolved_address",
        "//test/core/test_util:grpc_test_util",
    ],
)
