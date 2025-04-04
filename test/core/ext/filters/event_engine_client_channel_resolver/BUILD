# Copyright 2023 gRPC authors.
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

load("//bazel:grpc_build_system.bzl", "grpc_cc_proto_library", "grpc_internal_proto_library", "grpc_package")
load("//test/core/test_util:grpc_fuzzer.bzl", "grpc_fuzz_test")

grpc_package(name = "test/core/ext/filters/event_engine_client_channel_resolver")

licenses(["notice"])

grpc_internal_proto_library(
    name = "resolver_fuzzer_proto",
    srcs = ["resolver_fuzzer.proto"],
    deps = [
        "//test/core/test_util:fuzz_config_vars_proto",
        "//test/core/test_util:fuzzing_channel_args_proto",
    ],
)

grpc_cc_proto_library(
    name = "resolver_fuzzer_cc_proto",
    deps = ["resolver_fuzzer_proto"],
)

grpc_fuzz_test(
    name = "resolver_fuzzer",
    srcs = ["resolver_fuzzer.cc"],
    external_deps = [
        "absl/log:check",
        "gtest",
        "fuzztest",
        "fuzztest_main",
    ],
    tags = [
        "no_mac",
        "no_windows",
    ],
    deps = [
        "resolver_fuzzer_cc_proto",
        "//:gpr",
        "//:grpc",
        "//src/core:channel_args",
        "//test/core/event_engine:aborting_event_engine",
        "//test/core/event_engine/fuzzing_event_engine",
        "//test/core/test_util:fuzz_config_vars",
        "//test/core/test_util:fuzz_config_vars_helpers",
        "//test/core/test_util:fuzzing_channel_args",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
    ],
)
