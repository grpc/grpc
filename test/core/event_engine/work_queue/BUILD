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

load("//bazel:grpc_build_system.bzl", "grpc_cc_proto_library", "grpc_cc_test", "grpc_internal_proto_library", "grpc_package")
load("//test/core/test_util:grpc_fuzzer.bzl", "grpc_fuzz_test")

licenses(["notice"])

grpc_package(
    name = "test/core/event_engine/work_queue",
    visibility = "tests",
)

grpc_cc_test(
    name = "basic_work_queue_test",
    srcs = ["basic_work_queue_test.cc"],
    external_deps = ["gtest"],
    deps = [
        "//:exec_ctx",
        "//:gpr_platform",
        "//src/core:common_event_engine_closures",
        "//src/core:event_engine_basic_work_queue",
        "//test/core/test_util:grpc_test_util_unsecure",
    ],
)

grpc_internal_proto_library(
    name = "work_queue_fuzzer_proto",
    srcs = ["work_queue_fuzzer.proto"],
)

grpc_cc_proto_library(
    name = "work_queue_fuzzer_cc_proto",
    deps = ["work_queue_fuzzer_proto"],
)

# TODO(hork): the same fuzzer configuration should work trivially for all
# WorkQueue implementations. Generalize it when another implementation is
# written.
grpc_fuzz_test(
    name = "basic_work_queue_fuzzer",
    srcs = ["basic_work_queue_fuzzer.cc"],
    external_deps = [
        "gtest",
        "fuzztest",
        "fuzztest_main",
    ],
    deps = [
        "work_queue_fuzzer_cc_proto",
        "//src/core:event_engine_basic_work_queue",
        "//test/core/test_util:grpc_test_util",
    ],
)
