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

load("//bazel:grpc_build_system.bzl", "grpc_cc_library", "grpc_cc_test", "grpc_package")

licenses(["notice"])

grpc_package(name = "test/core/resource_quota")

load("//test/core/util:grpc_fuzzer.bzl", "grpc_proto_fuzzer")

grpc_cc_library(
    name = "call_checker",
    testonly = True,
    hdrs = ["call_checker.h"],
    language = "c++",
    deps = ["//:gpr"],
)

grpc_cc_test(
    name = "memory_quota_test",
    srcs = ["memory_quota_test.cc"],
    external_deps = [
        "gtest",
    ],
    language = "c++",
    uses_polling = False,
    deps = [
        "call_checker",
        "//:memory_quota",
        "//test/core/util:grpc_suppressions",
    ],
)

grpc_cc_test(
    name = "thread_quota_test",
    srcs = ["thread_quota_test.cc"],
    external_deps = [
        "gtest",
    ],
    language = "c++",
    uses_polling = False,
    deps = [
        "//:thread_quota",
        "//test/core/util:grpc_suppressions",
    ],
)

grpc_cc_test(
    name = "resource_quota_test",
    srcs = ["resource_quota_test.cc"],
    external_deps = [
        "gtest",
    ],
    language = "c++",
    uses_polling = False,
    deps = [
        "//:resource_quota",
        "//test/core/util:grpc_suppressions",
    ],
)

grpc_cc_test(
    name = "memory_quota_stress_test",
    srcs = ["memory_quota_stress_test.cc"],
    language = "c++",
    # We only run this test under Linux, and really only care about the
    # TSAN results.
    tags = [
        "no_mac",
        "no_windows",
    ],
    uses_polling = False,
    deps = [
        "//:memory_quota",
        "//test/core/util:grpc_suppressions",
    ],
)

grpc_proto_fuzzer(
    name = "memory_quota_fuzzer",
    srcs = ["memory_quota_fuzzer.cc"],
    corpus = "memory_quota_fuzzer_corpus",
    language = "C++",
    proto = "memory_quota_fuzzer.proto",
    tags = ["no_windows"],
    uses_polling = False,
    deps = [
        "call_checker",
        "//:memory_quota",
        "//test/core/util:grpc_test_util",
    ],
)
