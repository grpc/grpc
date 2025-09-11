# Copyright 2016 gRPC authors.
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
load("//test/core/test_util:grpc_fuzzer.bzl", "grpc_fuzz_test")

grpc_package(name = "test/core/channelz/zviz")

licenses(["notice"])

grpc_fuzz_test(
    name = "strings_test",
    srcs = ["strings_test.cc"],
    external_deps = [
        "absl/log:check",
        "fuzztest",
        "fuzztest_main",
    ],
    deps = [
        "//src/core:zviz_strings",
    ],
)

grpc_cc_library(
    name = "environment_fake",
    testonly = True,
    hdrs = ["environment_fake.h"],
    external_deps = [
        "absl/container:flat_hash_map",
    ],
    deps = [
        "//src/core:zviz_environment",
    ],
)

grpc_fuzz_test(
    name = "environment_test",
    srcs = ["environment_test.cc"],
    external_deps = [
        "absl/log:check",
        "fuzztest",
        "fuzztest_main",
    ],
    deps = [
        "environment_fake",
        "//src/core:zviz_environment",
    ],
)

grpc_cc_test(
    name = "layout_test",
    srcs = ["layout_test.cc"],
    external_deps = [
        "gtest",
        "gtest_main",
    ],
    deps = [
        "//src/core:zviz_environment",
        "//src/core:zviz_layout",
    ],
)

grpc_cc_library(
    name = "layout_log",
    testonly = True,
    hdrs = ["layout_log.h"],
    deps = [
        "//src/core:zviz_layout",
    ],
)

grpc_fuzz_test(
    name = "data_test",
    srcs = ["data_test.cc"],
    external_deps = [
        "absl/log:check",
        "fuzztest",
        "fuzztest_main",
    ],
    deps = [
        "environment_fake",
        "layout_log",
        "//src/core:zviz_data",
        "@com_google_protobuf//:protobuf",
    ],
)

grpc_fuzz_test(
    name = "trace_test",
    srcs = ["trace_test.cc"],
    external_deps = [
        "absl/log:check",
        "absl/container:flat_hash_map",
        "absl/strings",
        "fuzztest",
        "fuzztest_main",
    ],
    deps = [
        "environment_fake",
        "layout_log",
        "//src/core:zviz_trace",
        "//src/proto/grpc/channelz/v2:channelz_cc_proto",
        "@com_google_protobuf//:protobuf",
    ],
)

grpc_fuzz_test(
    name = "entity_test",
    srcs = ["entity_test.cc"],
    external_deps = [
        "absl/log:check",
        "absl/container:flat_hash_map",
        "absl/strings",
        "fuzztest",
        "fuzztest_main",
    ],
    deps = [
        "environment_fake",
        "layout_log",
        "//src/core:zviz_entity",
        "//src/proto/grpc/channelz/v2:channelz_cc_proto",
        "//src/proto/grpc/channelz/v2:property_list_cc_proto",
        "@com_google_protobuf//:protobuf",
    ],
)

grpc_fuzz_test(
    name = "html_test",
    srcs = ["html_test.cc"],
    external_deps = [
        "absl/log:check",
        "fuzztest",
        "fuzztest_main",
    ],
    deps = [
        "//src/core:zviz_html",
    ],
)

grpc_fuzz_test(
    name = "layout_html_test",
    srcs = ["layout_html_test.cc"],
    external_deps = [
        "absl/log:check",
        "fuzztest",
        "fuzztest_main",
    ],
    deps = [
        "//src/core:zviz_html",
        "//src/core:zviz_layout_html",
    ],
)

grpc_fuzz_test(
    name = "layout_text_test",
    srcs = ["layout_text_test.cc"],
    external_deps = [
        "gtest",
        "gtest_main",
    ],
    deps = [
        "//src/core:zviz_layout_text",
    ],
)

grpc_fuzz_test(
    name = "html_integration_test",
    srcs = ["html_integration_test.cc"],
    external_deps = [
        "absl/log:check",
        "fuzztest",
        "fuzztest_main",
        "absl/container:flat_hash_map",
    ],
    deps = [
        "environment_fake",
        "//src/core:zviz_entity",
        "//src/core:zviz_html",
        "//src/core:zviz_layout",
        "//src/core:zviz_layout_html",
        "//src/proto/grpc/channelz/v2:channelz_cc_proto",
        "@com_google_protobuf//:protobuf",
    ],
)

grpc_fuzz_test(
    name = "property_list_test",
    srcs = ["property_list_test.cc"],
    external_deps = [
        "absl/log:check",
        "fuzztest",
        "fuzztest_main",
        "gtest",
        "protobuf",
    ],
    deps = [
        "//src/core:zviz_property_list",
        "//src/proto/grpc/channelz/v2:channelz_cc_proto",
    ],
)

grpc_fuzz_test(
    name = "format_entity_list_test",
    srcs = ["format_entity_list_test.cc"],
    external_deps = [
        "absl/container:flat_hash_map",
        "absl/log:check",
        "absl/strings",
        "fuzztest",
        "fuzztest_main",
        "gtest",
        "protobuf",
    ],
    deps = [
        "environment_fake",
        "layout_log",
        "//src/core:zviz_format_entity_list",
        "//src/core:zviz_layout_html",
        "//src/proto/grpc/channelz/v2:channelz_cc_proto",
    ],
)
