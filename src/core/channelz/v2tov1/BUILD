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

load(
    "//bazel:grpc_build_system.bzl",
    "grpc_cc_library",
    "grpc_package",
)

licenses(["notice"])

grpc_package(
    name = "src/core/channelz/v2tov1",
    visibility = "grpc",
)

grpc_cc_library(
    name = "property_list",
    srcs = ["property_list.cc"],
    hdrs = ["property_list.h"],
    external_deps = [
        "absl/strings:string_view",
        "@com_google_protobuf//upb/mem",
    ],
    deps = [
        "//:channelz_property_list_upb",
        "//:protobuf_any_upb",
        "//:protobuf_duration_upb",
        "//:protobuf_timestamp_upb",
        "//src/core:upb_utils",
    ],
)

grpc_cc_library(
    name = "convert",
    srcs = ["convert.cc"],
    hdrs = ["convert.h"],
    external_deps = [
        "absl/cleanup",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "@com_google_protobuf//upb/base",
        "@com_google_protobuf//upb/mem",
        "@com_google_protobuf//upb/json",
        "@com_google_protobuf//upb/reflection",
    ],
    deps = [
        "property_list",
        "//:channelz_property_list_upb",
        "//:channelz_upb",
        "//:channelz_v1_upb",
        "//:channelz_v1_upbdefs",
        "//:parse_address",
        "//:protobuf_any_upb",
        "//:protobuf_duration_upb",
        "//:protobuf_timestamp_upb",
        "//:protobuf_wrappers_upb",
        "//:sockaddr_utils",
        "//:uri",
        "//src/core:json",
        "//src/core:json_writer",
        "//src/core:protobuf_wrappers_upb",
        "//src/core:upb_utils",
    ],
)

grpc_cc_library(
    name = "legacy_api",
    srcs = ["legacy_api.cc"],
    hdrs = ["legacy_api.h"],
    external_deps = [
        "absl/time",
        "absl/log",
        "absl/status:statusor",
        "absl/strings",
    ],
    deps = [
        "convert",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc_public_hdrs",
        "//src/core:experiments",
        "//src/core:json",
        "//src/core:json_reader",
        "//src/core:json_writer",
        "//src/core/channelz",
    ],
)
