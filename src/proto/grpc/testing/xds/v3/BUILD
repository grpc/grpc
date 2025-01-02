# Copyright 2020 gRPC authors.
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

load("//bazel:grpc_build_system.bzl", "grpc_cc_grpc_library", "grpc_cc_proto_library", "grpc_internal_proto_library", "grpc_package")

licenses(["notice"])

grpc_package(
    name = "xds_v3",
    visibility = "tests",
)

grpc_internal_proto_library(
    name = "discovery_proto",
    srcs = ["discovery.proto"],
    has_services = True,
    deps = [
        "@com_google_protobuf//:any_proto",
        "@com_google_protobuf//:duration_proto",
        "@envoy_api//envoy/config/core/v3:pkg",
    ],
)

grpc_cc_proto_library(
    name = "discovery_cc_proto",
    deps = ["discovery_proto"],
)

grpc_internal_proto_library(
    name = "ads_proto",
    srcs = ["ads.proto"],
    has_services = True,
    deps = ["discovery_proto"],
)

grpc_cc_proto_library(
    name = "ads_cc_proto",
    deps = ["ads_proto"],
)

grpc_cc_grpc_library(
    name = "ads_cc_grpc",
    srcs = ["ads_proto"],
    deps = ["ads_cc_proto"],
)

grpc_internal_proto_library(
    name = "lrs_proto",
    srcs = ["lrs.proto"],
    has_services = True,
    deps = [
        "@com_google_protobuf//:duration_proto",
        "@envoy_api//envoy/config/core/v3:pkg",
        "@envoy_api//envoy/config/endpoint/v3:pkg",
    ],
)

grpc_cc_proto_library(
    name = "lrs_cc_proto",
    deps = ["lrs_proto"],
)

grpc_cc_grpc_library(
    name = "lrs_cc_grpc",
    srcs = ["lrs_proto"],
    deps = ["lrs_cc_proto"],
)

grpc_internal_proto_library(
    name = "orca_service_proto",
    srcs = ["orca_service.proto"],
    has_services = True,
    deps = [
        "@com_github_cncf_xds//xds/data/orca/v3:pkg",
        "@com_google_protobuf//:duration_proto",
    ],
)

grpc_cc_proto_library(
    name = "orca_service_cc_proto",
    deps = ["orca_service_proto"],
)

grpc_cc_grpc_library(
    name = "orca_service_cc_grpc",
    srcs = ["orca_service_proto"],
    deps = ["orca_service_cc_proto"],
)

grpc_internal_proto_library(
    name = "csds_proto",
    srcs = ["csds.proto"],
    has_services = True,
    deps = [
        "@com_google_protobuf//:any_proto",
        "@com_google_protobuf//:timestamp_proto",
        "@envoy_api//envoy/admin/v3:pkg",
        "@envoy_api//envoy/config/core/v3:pkg",
    ],
)

grpc_cc_proto_library(
    name = "csds_cc_proto",
    deps = ["csds_proto"],
)

grpc_cc_grpc_library(
    name = "csds_cc_grpc",
    srcs = ["csds_proto"],
    # Needs to be visible to //:grpcpp_csds
    visibility = ["//:__subpackages__"],
    deps = ["csds_cc_proto"],
)
