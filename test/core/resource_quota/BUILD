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
load("//test/core/util:grpc_fuzzer.bzl", "grpc_proto_fuzzer")

licenses(["notice"])

grpc_package(name = "test/core/resource_quota")

grpc_cc_test(
    name = "arena_test",
    srcs = ["arena_test.cc"],
    external_deps = [
        "absl/strings",
        "gtest",
    ],
    language = "C++",
    tags = [
        "resource_quota_test",
    ],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:exec_ctx",
        "//:gpr",
        "//:ref_counted_ptr",
        "//src/core:arena",
        "//src/core:resource_quota",
        "//test/core/util:grpc_test_util_unsecure",
    ],
)

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
    external_deps = ["gtest"],
    language = "c++",
    tags = [
        "cpu:10",
        "resource_quota_test",
    ],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "call_checker",
        "//:exec_ctx",
        "//src/core:memory_quota",
        "//src/core:slice_refcount",
        "//test/core/util:grpc_test_util_unsecure",
    ],
)

grpc_cc_test(
    name = "periodic_update_test",
    srcs = ["periodic_update_test.cc"],
    external_deps = ["gtest"],
    flaky = True,
    language = "c++",
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:exec_ctx",
        "//:gpr",
        "//src/core:periodic_update",
    ],
)

grpc_cc_test(
    name = "thread_quota_test",
    srcs = ["thread_quota_test.cc"],
    external_deps = ["gtest"],
    language = "c++",
    uses_event_engine = False,
    uses_polling = False,
    deps = ["//src/core:thread_quota"],
)

grpc_cc_test(
    name = "resource_quota_test",
    srcs = ["resource_quota_test.cc"],
    external_deps = ["gtest"],
    language = "c++",
    tags = [
        "resource_quota_test",
    ],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//src/core:resource_quota",
        "//test/core/util:grpc_test_util_unsecure",
    ],
)

grpc_cc_test(
    name = "memory_quota_stress_test",
    srcs = ["memory_quota_stress_test.cc"],
    external_deps = [
        "absl/base:core_headers",
        "absl/strings",
        "absl/types:optional",
        "gtest",
    ],
    language = "c++",
    # We only run this test under Linux, and really only care about the
    # TSAN results.
    tags = [
        "no_mac",
        "no_windows",
        "resource_quota_test",
    ],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:exec_ctx",
        "//:gpr",
        "//src/core:event_engine_memory_allocator",
        "//src/core:memory_quota",
        "//test/core/util:grpc_test_util_unsecure",
    ],
)

grpc_proto_fuzzer(
    name = "memory_quota_fuzzer",
    srcs = ["memory_quota_fuzzer.cc"],
    corpus = "memory_quota_fuzzer_corpus",
    external_deps = [
        "absl/status",
        "absl/strings",
        "absl/types:optional",
    ],
    language = "C++",
    proto = "memory_quota_fuzzer.proto",
    proto_deps = ["//test/core/util:fuzz_config_vars_proto"],
    tags = ["no_windows"],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "call_checker",
        "//:debug_location",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc_trace",
        "//src/core:closure",
        "//src/core:error",
        "//src/core:event_engine_memory_allocator",
        "//src/core:experiments",
        "//src/core:memory_quota",
        "//src/core:useful",
        "//test/core/util:fuzz_config_vars",
    ],
)
