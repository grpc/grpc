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
    "grpc_cc_grpc_library",
    "grpc_cc_proto_library",
    "grpc_internal_proto_library",
    "grpc_package",
    "grpc_upb_proto_library",
    "grpc_upb_proto_reflection_library",
)

licenses(["notice"])

# TODO(rishesh): Enable parse_headers once when internal build system limitations are resolved.
grpc_package(
    name = "src/proto/grpc/channelz/v2",
    features = [
        "layering_check",
        # "parse_headers",
    ],
    visibility = "grpc",
)

grpc_internal_proto_library(
    name = "channelz_proto",
    srcs = ["channelz.proto"],
    deps = [
        "@com_google_protobuf//:any_proto",
        "@com_google_protobuf//:timestamp_proto",
    ],
)

grpc_upb_proto_library(
    name = "channelz_upb_proto",
    deps = ["channelz_proto"],
)

grpc_upb_proto_reflection_library(
    name = "channelz_upbdefs_proto",
    deps = ["channelz_proto"],
)

grpc_internal_proto_library(
    name = "property_list_proto",
    srcs = ["property_list.proto"],
    deps = [
        "@com_google_protobuf//:any_proto",
        "@com_google_protobuf//:duration_proto",
        "@com_google_protobuf//:empty_proto",
        "@com_google_protobuf//:timestamp_proto",
    ],
)

# TODO(roth): Migrate this to use separate
# grpc_internal_proto_library/grpc_cc_proto_library/grpc_cc_grpc_library
# rules when internal build system limitations are resolved.
grpc_internal_proto_library(
    name = "latent_see_proto",
    srcs = ["latent_see.proto"],
    has_services = True,
    deps = [
        ":property_list_proto",
    ],
)

grpc_upb_proto_library(
    name = "property_list_upb_proto",
    deps = ["property_list_proto"],
)

grpc_cc_proto_library(
    name = "latent_see_cc_proto",
    deps = [":latent_see_proto"],
)

grpc_cc_grpc_library(
    name = "latent_see_cc_grpc",
    srcs = [":latent_see_proto"],
    deps = [":latent_see_cc_proto"],
)

grpc_internal_proto_library(
    name = "promise_proto",
    srcs = ["promise.proto"],
    deps = [
        "property_list_proto",
    ],
)

grpc_cc_proto_library(
    name = "promise_cc_proto",
    deps = [":promise_proto"],
)

grpc_cc_proto_library(
    name = "channelz_cc_proto",
    visibility = [
        "//bazel:channelz",
    ],
    deps = [":channelz_proto"],
)

grpc_cc_proto_library(
    name = "property_list_cc_proto",
    deps = [":property_list_proto"],
)

grpc_upb_proto_library(
    name = "promise_upb_proto",
    deps = [":promise_proto"],
)

grpc_internal_proto_library(
    name = "service_proto",
    srcs = ["service.proto"],
    has_services = True,
    deps = [
        ":channelz_proto",
        "@com_google_protobuf//:any_proto",
    ],
)

grpc_cc_proto_library(
    name = "service_cc_proto",
    visibility = [
        "//bazel:channelz",
    ],
    deps = [":service_proto"],
)

grpc_cc_grpc_library(
    name = "service_cc_grpc",
    srcs = [":service_proto"],
    visibility = [
        "//bazel:channelz",
    ],
    deps = [
        ":service_cc_proto",
    ],
)
