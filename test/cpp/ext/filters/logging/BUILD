# gRPC Bazel BUILD file.
#
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

load("//bazel:grpc_build_system.bzl", "grpc_cc_library", "grpc_cc_test", "grpc_package")

licenses(["notice"])

grpc_package(name = "test/cpp/ext/filters/logging")

grpc_cc_library(
    name = "logging_test_library",
    testonly = 1,
    srcs = ["library.cc"],
    hdrs = ["library.h"],
    external_deps = [
        "gtest",
    ],
    language = "C++",
    deps = [
        "//:grpc++",
        "//src/core:logging_filter",
        "//src/cpp/ext/gcp:observability_logging_sink",
        "//src/proto/grpc/testing:echo_proto",
        "//test/core/util:grpc_test_util",
        "//test/cpp/end2end:test_service_impl",
        "//test/cpp/util:test_config",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_test(
    name = "logging_test",
    srcs = [
        "logging_test.cc",
    ],
    external_deps = [
        "gtest",
    ],
    language = "C++",
    tags = [
        "logging_test",
        "no_mac",  # TODO(yashykt): The default stack-sizes are not enough to run this test on MacOS
    ],
    deps = [
        ":logging_test_library",
        "//:grpc++",
        "//src/core:logging_filter",
        "//src/cpp/ext/gcp:observability_logging_sink",
        "//src/proto/grpc/testing:echo_proto",
        "//test/core/util:grpc_test_util",
        "//test/cpp/end2end:test_service_impl",
        "//test/cpp/util:test_config",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_test(
    name = "logging_census_integration_test",
    srcs = [
        "logging_census_integration_test.cc",
    ],
    external_deps = [
        "gtest",
    ],
    language = "C++",
    tags = [
        "logging_test",
        "no_mac",  # TODO(yashykt): The default stack-sizes are not enough to run this test on MacOS
    ],
    deps = [
        ":logging_test_library",
        "//:grpc++",
        "//src/core:logging_filter",
        "//src/cpp/ext/gcp:observability_logging_sink",
        "//src/proto/grpc/testing:echo_proto",
        "//test/core/util:grpc_test_util",
        "//test/cpp/end2end:test_service_impl",
        "//test/cpp/ext/filters/census:library",
        "//test/cpp/util:test_config",
        "//test/cpp/util:test_util",
    ],
)
