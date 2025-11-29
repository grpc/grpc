# Copyright 2025 gRPC authors.
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

load(
    "//bazel:grpc_build_system.bzl",
    "grpc_package",
)
load(
    "//test/core/test_util:grpc_fuzzer.bzl",
    "grpc_fuzz_test",
)

grpc_package(name = "test/core/channelz/v2tov1")

licenses(["notice"])

grpc_fuzz_test(
    name = "property_list_test",
    srcs = ["property_list_test.cc"],
    external_deps = [
        "gtest",
        "fuzztest",
        "fuzztest_main",
        "absl/log:check",
        "@com_google_protobuf//upb/mem",
    ],
    deps = [
        "//:channelz_property_list_upb",
        "//:grpc",
        "//:protobuf_duration_upb",
        "//:protobuf_timestamp_upb",
        "//src/core:channelz_v2tov1_property_list",
        "//src/proto/grpc/channelz/v2:property_list_cc_proto",
        "//test/core/test_util:grpc_test_util",
        "@com_google_protobuf//:protobuf",
    ],
)

grpc_fuzz_test(
    name = "convert_test",
    srcs = ["convert_test.cc"],
    external_deps = [
        "gtest",
        "absl/container:flat_hash_map",
        "absl/log:check",
        "absl/status",
        "fuzztest",
        "fuzztest_main",
    ],
    deps = [
        "//:channelz_upb",
        "//:channelz_upbdefs",
        "//:grpc",
        "//src/core:channelz_v2tov1_convert",
        "//src/core:json",
        "//src/core:json_reader",
        "//src/proto/grpc/channelz:channelz_cc_proto",
        "//src/proto/grpc/channelz/v2:channelz_cc_proto",
        "//src/proto/grpc/channelz/v2:property_list_cc_proto",
        "//test/core/test_util:grpc_test_util",
        "@com_google_protobuf//:protobuf",
    ],
)
