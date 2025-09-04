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
    name = "src/core/channelz/zviz",
    visibility = "grpc",
)

grpc_cc_library(
    name = "strings",
    srcs = [
        "strings.cc",
    ],
    hdrs = ["strings.h"],
    external_deps = [
        "absl/strings",
    ],
)

grpc_cc_library(
    name = "environment",
    srcs = [
        "environment.cc",
    ],
    hdrs = ["environment.h"],
    external_deps = [
        "absl/strings",
        "absl/status:statusor",
    ],
    deps = [
        "strings",
        "//src/proto/grpc/channelz/v2:channelz_cc_proto",
    ],
)

grpc_cc_library(
    name = "layout",
    srcs = [
        "layout.cc",
    ],
    hdrs = ["layout.h"],
    external_deps = [
        "absl/strings",
    ],
    deps = [
        "environment",
        "//src/proto/grpc/channelz/v2:channelz_cc_proto",
        "@com_google_protobuf//:duration_cc_proto",
        "@com_google_protobuf//:timestamp_cc_proto",
    ],
)

grpc_cc_library(
    name = "data",
    srcs = [
        "data.cc",
    ],
    hdrs = ["data.h"],
    external_deps = [
        "absl/container:flat_hash_map",
        "absl/strings",
        "absl/types:span",
    ],
    deps = [
        "environment",
        "layout",
        "//src/core:no_destruct",
        "//src/proto/grpc/channelz/v2:channelz_cc_proto",
        "//src/proto/grpc/channelz/v2:property_list_cc_proto",
    ],
)

grpc_cc_library(
    name = "trace",
    srcs = [
        "trace.cc",
    ],
    hdrs = ["trace.h"],
    external_deps = [
        "absl/container:flat_hash_map",
        "absl/strings",
        "absl/types:span",
    ],
    deps = [
        "data",
        "environment",
        "layout",
        "//src/proto/grpc/channelz/v2:channelz_cc_proto",
    ],
)

grpc_cc_library(
    name = "property_list",
    srcs = [
        "property_list.cc",
    ],
    hdrs = ["property_list.h"],
    external_deps = [
        "absl/strings",
        "absl/time",
    ],
    deps = [
        "//src/proto/grpc/channelz/v2:channelz_cc_proto",
        "//src/proto/grpc/channelz/v2:property_list_cc_proto",
    ],
)

grpc_cc_library(
    name = "style",
    srcs = [
        "style.cc",
    ],
    hdrs = ["style.h"],
    external_deps = [
        "absl/strings:string_view",
    ],
)

grpc_cc_library(
    name = "entity",
    srcs = [
        "entity.cc",
    ],
    hdrs = ["entity.h"],
    external_deps = [
        "absl/container:flat_hash_map",
        "absl/strings",
        "absl/types:span",
    ],
    deps = [
        "data",
        "environment",
        "layout",
        "strings",
        "style",
        "trace",
        "//src/proto/grpc/channelz/v2:channelz_cc_proto",
    ],
)

grpc_cc_library(
    name = "html",
    srcs = [
        "html.cc",
    ],
    hdrs = ["html.h"],
    external_deps = [
        "absl/container:flat_hash_map",
        "absl/strings",
        "absl/strings:string_view",
        "absl/types:span",
        "absl/functional:function_ref",
        "absl/log:check",
    ],
)

grpc_cc_library(
    name = "layout_html",
    srcs = [
        "layout_html.cc",
    ],
    hdrs = ["layout_html.h"],
    external_deps = [
        "absl/container:flat_hash_map",
        "absl/strings",
        "absl/strings:string_view",
        "absl/types:span",
        "absl/functional:function_ref",
    ],
    deps = [
        "environment",
        "html",
        "layout",
    ],
)

grpc_cc_library(
    name = "format_entity_list",
    srcs = [
        "format_entity_list.cc",
    ],
    hdrs = ["format_entity_list.h"],
    external_deps = [
        "absl/strings",
        "absl/types:optional",
        "absl/types:span",
    ],
    deps = [
        "environment",
        "layout",
        "property_list",
        "//src/core:time",
        "//src/proto/grpc/channelz/v2:channelz_cc_proto",
        "//src/proto/grpc/channelz/v2:property_list_cc_proto",
        "@com_google_protobuf//:any_cc_proto",
    ],
)
