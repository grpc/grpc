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

licenses(["notice"])  # Apache v2

load("//bazel:grpc_build_system.bzl", "grpc_package", "grpc_proto_library")

grpc_package(
    name = "xds_v3",
    visibility = "public",
)

grpc_proto_library(
    name = "address_proto",
    srcs = [
        "address.proto",
    ],
)

grpc_proto_library(
    name = "percent_proto",
    srcs = [
        "percent.proto",
    ],
)

grpc_proto_library(
    name = "base_proto",
    srcs = [
        "base.proto",
    ],
    well_known_protos = True,
    deps = [
        "percent_proto",
    ],
)

grpc_proto_library(
    name = "discovery_proto",
    srcs = [
        "discovery.proto",
    ],
    well_known_protos = True,
    deps = [
        "base_proto",
    ],
)

grpc_proto_library(
    name = "ads_proto",
    srcs = [
        "ads.proto",
    ],
    has_services = True,
    well_known_protos = True,
    deps = [
        "discovery_proto",
    ],
)

grpc_proto_library(
    name = "config_source_proto",
    srcs = [
        "config_source.proto",
    ],
)

grpc_proto_library(
    name = "cluster_proto",
    srcs = [
        "cluster.proto",
    ],
    well_known_protos = True,
    deps = [
        "config_source_proto",
    ],
)

grpc_proto_library(
    name = "endpoint_proto",
    srcs = [
        "endpoint.proto",
    ],
    well_known_protos = True,
    deps = [
        "address_proto",
        "base_proto",
        "percent_proto",
    ],
)

grpc_proto_library(
    name = "listener_proto",
    srcs = [
        "listener.proto",
    ],
    well_known_protos = True,
)

grpc_proto_library(
    name = "load_report_proto",
    srcs = [
        "load_report.proto",
    ],
    well_known_protos = True,
    deps = [
        "address_proto",
        "base_proto",
    ],
)

grpc_proto_library(
    name = "lrs_proto",
    srcs = [
        "lrs.proto",
    ],
    has_services = True,
    well_known_protos = True,
    deps = [
        "base_proto",
        "load_report_proto",
    ],
)

grpc_proto_library(
    name = "orca_load_report_proto",
    srcs = [
        "orca_load_report.proto",
    ],
)

grpc_proto_library(
    name = "protocol_proto",
    srcs = [
        "protocol.proto",
    ],
    well_known_protos = True,
)

grpc_proto_library(
    name = "range_proto",
    srcs = [
        "range.proto",
    ],
)

grpc_proto_library(
    name = "regex_proto",
    srcs = [
        "regex.proto",
    ],
)

grpc_proto_library(
    name = "route_proto",
    srcs = [
        "route.proto",
    ],
    well_known_protos = True,
    deps = [
        "base_proto",
        "percent_proto",
        "range_proto",
        "regex_proto",
    ],
)

grpc_proto_library(
    name = "http_connection_manager_proto",
    srcs = [
        "http_connection_manager.proto",
    ],
    deps = [
        "config_source_proto",
        "protocol_proto",
        "route_proto",
    ],
)
