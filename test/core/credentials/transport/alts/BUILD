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

grpc_package(name = "test/core/credentials/transport/alts")

grpc_fuzz_test(
    name = "alts_credentials_fuzzer",
    srcs = ["alts_credentials_fuzzer.cc"],
    external_deps = [
        "absl/log:check",
        "fuzztest",
        "fuzztest_main",
    ],
    tags = ["no_windows"],
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
    ],
)

grpc_cc_test(
    name = "grpc_alts_credentials_options_test",
    srcs = ["grpc_alts_credentials_options_test.cc"],
    external_deps = ["gtest"],
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
    ],
)

grpc_cc_test(
    name = "alts_security_connector_test",
    srcs = ["alts_security_connector_test.cc"],
    external_deps = [
        "absl/log:log",
        "gtest",
    ],
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc_base",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
    ],
)

grpc_cc_test(
    name = "check_gcp_environment_linux_test",
    srcs = ["check_gcp_environment_linux_test.cc"],
    external_deps = ["gtest"],
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
    ],
)

grpc_cc_test(
    name = "check_gcp_environment_windows_test",
    srcs = ["check_gcp_environment_windows_test.cc"],
    external_deps = ["gtest"],
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
    ],
)
