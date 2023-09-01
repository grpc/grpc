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

load("//bazel:grpc_build_system.bzl", "grpc_package", "grpc_proto_library")
load("//bazel:python_rules.bzl", "py_grpc_library", "py_proto_library")

licenses(["notice"])

exports_files([
    "orca_load_report.proto",
])

grpc_package(
    name = "xds_v3",
    visibility = "public",
)

grpc_proto_library(
    name = "address_proto",
    srcs = [
        "address.proto",
    ],
    well_known_protos = True,
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
        "base_proto",
        "config_source_proto",
        "endpoint_proto",
        "extension_proto",
        "health_check_proto",
        "outlier_detection_proto",
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
        "health_check_proto",
        "percent_proto",
    ],
)

grpc_proto_library(
    name = "health_check_proto",
    srcs = [
        "health_check.proto",
    ],
)

grpc_proto_library(
    name = "extension_proto",
    srcs = [
        "extension.proto",
    ],
    well_known_protos = True,
)

grpc_proto_library(
    name = "path_proto",
    srcs = [
        "path.proto",
    ],
    well_known_protos = True,
    deps = [
        "string_proto",
    ],
)

grpc_proto_library(
    name = "listener_proto",
    srcs = [
        "listener.proto",
    ],
    well_known_protos = True,
    deps = [
        "address_proto",
        "base_proto",
    ],
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
    name = "orca_service_proto",
    srcs = [
        "orca_service.proto",
    ],
    has_services = True,
    well_known_protos = True,
    deps = [
        "orca_load_report_proto",
    ],
)

grpc_proto_library(
    name = "outlier_detection_proto",
    srcs = [
        "outlier_detection.proto",
    ],
    has_services = True,
    well_known_protos = True,
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
        "extension_proto",
        "percent_proto",
        "range_proto",
        "regex_proto",
        "string_proto",
    ],
)

grpc_proto_library(
    name = "http_connection_manager_proto",
    srcs = [
        "http_connection_manager.proto",
    ],
    well_known_protos = True,
    deps = [
        "config_source_proto",
        "extension_proto",
        "protocol_proto",
        "route_proto",
    ],
)

grpc_proto_library(
    name = "router_proto",
    srcs = [
        "router.proto",
    ],
)

grpc_proto_library(
    name = "string_proto",
    srcs = [
        "string.proto",
    ],
    well_known_protos = True,
    deps = [
        "regex_proto",
    ],
)

grpc_proto_library(
    name = "aggregate_cluster_proto",
    srcs = [
        "aggregate_cluster.proto",
    ],
    well_known_protos = True,
    deps = [
        "string_proto",
    ],
)

grpc_proto_library(
    name = "fault_common_proto",
    srcs = [
        "fault_common.proto",
    ],
    well_known_protos = True,
    deps = [
        "percent_proto",
    ],
)

grpc_proto_library(
    name = "tls_proto",
    srcs = [
        "tls.proto",
    ],
    well_known_protos = True,
    deps = [
        "base_proto",
        "extension_proto",
        "string_proto",
    ],
)

grpc_proto_library(
    name = "config_dump_proto",
    srcs = [
        "config_dump.proto",
    ],
    well_known_protos = True,
)

grpc_proto_library(
    name = "csds_proto",
    srcs = [
        "csds.proto",
    ],
    well_known_protos = True,
    deps = [
        "base_proto",
        "config_dump_proto",
    ],
)

grpc_proto_library(
    name = "fault_proto",
    srcs = [
        "fault.proto",
    ],
    well_known_protos = True,
    deps = [
        "fault_common_proto",
        "percent_proto",
        "route_proto",
    ],
)

grpc_proto_library(
    name = "metadata_proto",
    srcs = [
        "metadata.proto",
    ],
    well_known_protos = True,
)

grpc_proto_library(
    name = "expr_proto",
    srcs = [
        "expr.proto",
    ],
    well_known_protos = True,
)

grpc_proto_library(
    name = "rbac_proto",
    srcs = [
        "rbac.proto",
    ],
    well_known_protos = True,
    deps = [
        "address_proto",
        "expr_proto",
        "extension_proto",
        "metadata_proto",
        "path_proto",
        "range_proto",
        "route_proto",
        "string_proto",
    ],
)

grpc_proto_library(
    name = "http_filter_rbac_proto",
    srcs = [
        "http_filter_rbac.proto",
    ],
    well_known_protos = True,
    deps = [
        "rbac_proto",
    ],
)

# Contains stdout audit logger.
grpc_proto_library(
    name = "audit_logger_stream_proto",
    srcs = [
        "audit_logger_stream.proto",
    ],
)

grpc_proto_library(
    name = "client_side_weighted_round_robin_proto",
    srcs = [
        "client_side_weighted_round_robin.proto",
    ],
    well_known_protos = True,
)

grpc_proto_library(
    name = "pick_first_proto",
    srcs = [
        "pick_first.proto",
    ],
    well_known_protos = True,
)

grpc_proto_library(
    name = "ring_hash_proto",
    srcs = [
        "ring_hash.proto",
    ],
    well_known_protos = True,
)

grpc_proto_library(
    name = "round_robin_proto",
    srcs = [
        "round_robin.proto",
    ],
    well_known_protos = True,
)

grpc_proto_library(
    name = "wrr_locality_proto",
    srcs = [
        "wrr_locality.proto",
    ],
    well_known_protos = True,
    deps = [
        "cluster_proto",
    ],
)

grpc_proto_library(
    name = "typed_struct_proto",
    srcs = [
        "typed_struct.proto",
    ],
    well_known_protos = True,
)

grpc_proto_library(
    name = "udpa_typed_struct_proto",
    srcs = [
        "udpa_typed_struct.proto",
    ],
    well_known_protos = True,
)

grpc_proto_library(
    name = "cookie_proto",
    srcs = [
        "cookie.proto",
    ],
    well_known_protos = True,
)

grpc_proto_library(
    name = "stateful_session_cookie_proto",
    srcs = [
        "stateful_session_cookie.proto",
    ],
    deps = [
        "cookie_proto",
    ],
)

grpc_proto_library(
    name = "stateful_session_proto",
    srcs = [
        "stateful_session.proto",
    ],
    deps = [
        "extension_proto",
    ],
)

py_proto_library(
    name = "csds_py_pb2",
    deps = [":_csds_proto_only"],
)

py_grpc_library(
    name = "csds_py_pb2_grpc",
    srcs = [":_csds_proto_only"],
    deps = [":csds_py_pb2"],
)

py_proto_library(
    name = "config_dump_py_pb2",
    deps = [":_config_dump_proto_only"],
)

py_proto_library(
    name = "base_py_pb2",
    deps = [":_base_proto_only"],
)

py_proto_library(
    name = "percent_py_pb2",
    deps = [":_percent_proto_only"],
)
