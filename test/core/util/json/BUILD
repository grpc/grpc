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

grpc_package(name = "test/core/util/json")

licenses(["notice"])

grpc_fuzz_test(
    name = "json_fuzz_test",
    srcs = ["json_fuzz_test.cc"],
    external_deps = [
        "absl/log:check",
        "gtest",
        "fuzztest",
        "fuzztest_main",
    ],
    tags = ["no_windows"],
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "json_test",
    srcs = ["json_test.cc"],
    external_deps = [
        "absl/log:log",
        "gtest",
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
    name = "json_object_loader_test",
    srcs = ["json_object_loader_test.cc"],
    external_deps = [
        "gtest",
    ],
    uses_polling = False,
    deps = [
        "//src/core:json_object_loader",
        "//test/core/test_util:grpc_test_util",
    ],
)
