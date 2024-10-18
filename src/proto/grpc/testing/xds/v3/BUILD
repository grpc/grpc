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
load("//bazel:python_rules.bzl", "py_grpc_library", "py_proto_library")

licenses(["notice"])

exports_files([
    "orca_load_report.proto",
])

grpc_package(
    name = "xds_v3",
    visibility = "tests",
)

grpc_internal_proto_library(
    name = "address_proto",
    srcs = ["address.proto"],
    deps = [
        "@com_google_protobuf//:wrappers_proto",
    ],
)

grpc_cc_proto_library(
    name = "address_cc_proto",
    deps = ["address_proto"],
)

grpc_internal_proto_library(
    name = "percent_proto",
    srcs = ["percent.proto"],
)

grpc_cc_proto_library(
    name = "percent_cc_proto",
    deps = ["percent_proto"],
)

grpc_internal_proto_library(
    name = "base_proto",
    srcs = ["base.proto"],
    deps = [
        "percent_proto",
        "@com_google_protobuf//:any_proto",
        "@com_google_protobuf//:struct_proto",
    ],
)

grpc_cc_proto_library(
    name = "base_cc_proto",
    deps = ["base_proto"],
)

grpc_internal_proto_library(
    name = "discovery_proto",
    srcs = ["discovery.proto"],
    has_services = True,
    deps = [
        "base_proto",
        "@com_google_protobuf//:any_proto",
        "@com_google_protobuf//:duration_proto",
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
    deps = [
        "discovery_proto",
    ],
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
    name = "config_source_proto",
    srcs = ["config_source.proto"],
)

grpc_cc_proto_library(
    name = "config_source_cc_proto",
    deps = ["config_source_proto"],
)

grpc_internal_proto_library(
    name = "cluster_proto",
    srcs = ["cluster.proto"],
    deps = [
        "base_proto",
        "config_source_proto",
        "endpoint_proto",
        "extension_proto",
        "health_check_proto",
        "outlier_detection_proto",
        "@com_google_protobuf//:any_proto",
        "@com_google_protobuf//:wrappers_proto",
    ],
)

grpc_cc_proto_library(
    name = "cluster_cc_proto",
    deps = ["cluster_proto"],
)

grpc_internal_proto_library(
    name = "endpoint_proto",
    srcs = ["endpoint.proto"],
    deps = [
        "address_proto",
        "base_proto",
        "health_check_proto",
        "percent_proto",
        "@com_google_protobuf//:wrappers_proto",
    ],
)

grpc_cc_proto_library(
    name = "endpoint_cc_proto",
    deps = ["endpoint_proto"],
)

grpc_internal_proto_library(
    name = "health_check_proto",
    srcs = ["health_check.proto"],
)

grpc_cc_proto_library(
    name = "health_check_cc_proto",
    deps = ["health_check_proto"],
)

grpc_internal_proto_library(
    name = "extension_proto",
    srcs = ["extension.proto"],
    deps = [
        "@com_google_protobuf//:any_proto",
    ],
)

grpc_cc_proto_library(
    name = "extension_cc_proto",
    deps = ["extension_proto"],
)

grpc_internal_proto_library(
    name = "path_proto",
    srcs = ["path.proto"],
    deps = [
        "string_proto",
    ],
)

grpc_cc_proto_library(
    name = "path_cc_proto",
    deps = ["path_proto"],
)

grpc_internal_proto_library(
    name = "listener_proto",
    srcs = ["listener.proto"],
    deps = [
        "address_proto",
        "base_proto",
        "@com_google_protobuf//:any_proto",
        "@com_google_protobuf//:wrappers_proto",
    ],
)

grpc_cc_proto_library(
    name = "listener_cc_proto",
    deps = ["listener_proto"],
)

grpc_internal_proto_library(
    name = "load_report_proto",
    srcs = ["load_report.proto"],
    deps = [
        "address_proto",
        "base_proto",
        "@com_google_protobuf//:duration_proto",
        "@com_google_protobuf//:struct_proto",
    ],
)

grpc_cc_proto_library(
    name = "load_report_cc_proto",
    deps = ["load_report_proto"],
)

grpc_internal_proto_library(
    name = "lrs_proto",
    srcs = ["lrs.proto"],
    has_services = True,
    deps = [
        "base_proto",
        "load_report_proto",
        "@com_google_protobuf//:duration_proto",
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
    name = "orca_load_report_proto",
    srcs = ["orca_load_report.proto"],
)

grpc_cc_proto_library(
    name = "orca_load_report_cc_proto",
    deps = ["orca_load_report_proto"],
)

grpc_internal_proto_library(
    name = "orca_service_proto",
    srcs = ["orca_service.proto"],
    has_services = True,
    deps = [
        "orca_load_report_proto",
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
    name = "outlier_detection_proto",
    srcs = ["outlier_detection.proto"],
    deps = [
        "@com_google_protobuf//:duration_proto",
        "@com_google_protobuf//:wrappers_proto",
    ],
)

grpc_cc_proto_library(
    name = "outlier_detection_cc_proto",
    deps = ["outlier_detection_proto"],
)

grpc_internal_proto_library(
    name = "protocol_proto",
    srcs = ["protocol.proto"],
    deps = [
        "@com_google_protobuf//:duration_proto",
    ],
)

grpc_cc_proto_library(
    name = "protocol_cc_proto",
    deps = ["protocol_proto"],
)

grpc_internal_proto_library(
    name = "http_protocol_options_proto",
    srcs = ["http_protocol_options.proto"],
    deps = [
        "protocol_proto",
    ],
)

grpc_cc_proto_library(
    name = "http_protocol_options_cc_proto",
    deps = ["http_protocol_options_proto"],
)

grpc_internal_proto_library(
    name = "range_proto",
    srcs = ["range.proto"],
)

grpc_cc_proto_library(
    name = "range_cc_proto",
    deps = ["range_proto"],
)

grpc_internal_proto_library(
    name = "regex_proto",
    srcs = ["regex.proto"],
)

grpc_cc_proto_library(
    name = "regex_cc_proto",
    deps = ["regex_proto"],
)

grpc_internal_proto_library(
    name = "route_proto",
    srcs = ["route.proto"],
    deps = [
        "base_proto",
        "extension_proto",
        "percent_proto",
        "range_proto",
        "regex_proto",
        "string_proto",
        "@com_google_protobuf//:any_proto",
        "@com_google_protobuf//:duration_proto",
        "@com_google_protobuf//:wrappers_proto",
    ],
)

grpc_cc_proto_library(
    name = "route_cc_proto",
    deps = ["route_proto"],
)

grpc_internal_proto_library(
    name = "http_connection_manager_proto",
    srcs = ["http_connection_manager.proto"],
    deps = [
        "config_source_proto",
        "extension_proto",
        "protocol_proto",
        "route_proto",
        "@com_google_protobuf//:any_proto",
    ],
)

grpc_cc_proto_library(
    name = "http_connection_manager_cc_proto",
    deps = ["http_connection_manager_proto"],
)

grpc_internal_proto_library(
    name = "router_proto",
    srcs = ["router.proto"],
)

grpc_cc_proto_library(
    name = "router_cc_proto",
    deps = ["router_proto"],
)

grpc_internal_proto_library(
    name = "string_proto",
    srcs = ["string.proto"],
    deps = [
        "regex_proto",
    ],
)

grpc_cc_proto_library(
    name = "string_cc_proto",
    deps = ["string_proto"],
)

grpc_internal_proto_library(
    name = "aggregate_cluster_proto",
    srcs = ["aggregate_cluster.proto"],
    deps = [
        "string_proto",
    ],
)

grpc_cc_proto_library(
    name = "aggregate_cluster_cc_proto",
    deps = ["aggregate_cluster_proto"],
)

grpc_internal_proto_library(
    name = "fault_common_proto",
    srcs = ["fault_common.proto"],
    deps = [
        "percent_proto",
        "@com_google_protobuf//:duration_proto",
    ],
)

grpc_cc_proto_library(
    name = "fault_common_cc_proto",
    deps = ["fault_common_proto"],
)

grpc_internal_proto_library(
    name = "tls_proto",
    srcs = ["tls.proto"],
    deps = [
        "base_proto",
        "extension_proto",
        "string_proto",
        "@com_google_protobuf//:wrappers_proto",
    ],
)

grpc_cc_proto_library(
    name = "tls_cc_proto",
    deps = ["tls_proto"],
)

grpc_internal_proto_library(
    name = "upstream_http_11_connect_proto",
    srcs = ["upstream_http_11_connect.proto"],
    deps = [
        "base_proto",
    ],
)

grpc_cc_proto_library(
    name = "upstream_http_11_connect_cc_proto",
    deps = ["upstream_http_11_connect_proto"],
)

grpc_internal_proto_library(
    name = "config_dump_proto",
    srcs = ["config_dump.proto"],
    deps = [
        "@com_google_protobuf//:any_proto",
        "@com_google_protobuf//:timestamp_proto",
    ],
)

grpc_cc_proto_library(
    name = "config_dump_cc_proto",
    deps = ["config_dump_proto"],
)

grpc_internal_proto_library(
    name = "csds_proto",
    srcs = ["csds.proto"],
    has_services = True,
    deps = [
        "base_proto",
        "config_dump_proto",
        "@com_google_protobuf//:any_proto",
        "@com_google_protobuf//:timestamp_proto",
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

grpc_internal_proto_library(
    name = "fault_proto",
    srcs = ["fault.proto"],
    deps = [
        "fault_common_proto",
        "percent_proto",
        "route_proto",
        "@com_google_protobuf//:wrappers_proto",
    ],
)

grpc_cc_proto_library(
    name = "fault_cc_proto",
    deps = ["fault_proto"],
)

grpc_internal_proto_library(
    name = "gcp_authn_proto",
    srcs = ["gcp_authn.proto"],
    deps = [
        "@com_google_protobuf//:wrappers_proto",
    ],
)

grpc_cc_proto_library(
    name = "gcp_authn_cc_proto",
    deps = ["gcp_authn_proto"],
)

grpc_internal_proto_library(
    name = "metadata_proto",
    srcs = ["metadata.proto"],
)

grpc_cc_proto_library(
    name = "metadata_cc_proto",
    deps = ["metadata_proto"],
)

grpc_internal_proto_library(
    name = "expr_proto",
    srcs = ["expr.proto"],
)

grpc_cc_proto_library(
    name = "expr_cc_proto",
    deps = ["expr_proto"],
)

grpc_internal_proto_library(
    name = "rbac_proto",
    srcs = ["rbac.proto"],
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

grpc_cc_proto_library(
    name = "rbac_cc_proto",
    deps = ["rbac_proto"],
)

grpc_internal_proto_library(
    name = "http_filter_rbac_proto",
    srcs = ["http_filter_rbac.proto"],
    deps = [
        "rbac_proto",
    ],
)

grpc_cc_proto_library(
    name = "http_filter_rbac_cc_proto",
    deps = ["http_filter_rbac_proto"],
)

grpc_internal_proto_library(
    name = "audit_logger_stream_proto",
    srcs = ["audit_logger_stream.proto"],
)

grpc_cc_proto_library(
    name = "audit_logger_stream_cc_proto",
    deps = ["audit_logger_stream_proto"],
)

grpc_internal_proto_library(
    name = "client_side_weighted_round_robin_proto",
    srcs = ["client_side_weighted_round_robin.proto"],
    deps = [
        "@com_google_protobuf//:duration_proto",
        "@com_google_protobuf//:wrappers_proto",
    ],
)

grpc_cc_proto_library(
    name = "client_side_weighted_round_robin_cc_proto",
    deps = ["client_side_weighted_round_robin_proto"],
)

grpc_internal_proto_library(
    name = "pick_first_proto",
    srcs = ["pick_first.proto"],
)

grpc_cc_proto_library(
    name = "pick_first_cc_proto",
    deps = ["pick_first_proto"],
)

grpc_internal_proto_library(
    name = "ring_hash_proto",
    srcs = ["ring_hash.proto"],
    deps = [
        "@com_google_protobuf//:wrappers_proto",
    ],
)

grpc_cc_proto_library(
    name = "ring_hash_cc_proto",
    deps = ["ring_hash_proto"],
)

grpc_internal_proto_library(
    name = "round_robin_proto",
    srcs = ["round_robin.proto"],
)

grpc_cc_proto_library(
    name = "round_robin_cc_proto",
    deps = ["round_robin_proto"],
)

grpc_internal_proto_library(
    name = "wrr_locality_proto",
    srcs = ["wrr_locality.proto"],
    deps = [
        "cluster_proto",
    ],
)

grpc_cc_proto_library(
    name = "wrr_locality_cc_proto",
    deps = ["wrr_locality_proto"],
)

grpc_internal_proto_library(
    name = "typed_struct_proto",
    srcs = ["typed_struct.proto"],
    deps = [
        "@com_google_protobuf//:struct_proto",
    ],
)

grpc_cc_proto_library(
    name = "typed_struct_cc_proto",
    deps = ["typed_struct_proto"],
)

grpc_internal_proto_library(
    name = "udpa_typed_struct_proto",
    srcs = ["udpa_typed_struct.proto"],
    deps = [
        "@com_google_protobuf//:struct_proto",
    ],
)

grpc_cc_proto_library(
    name = "udpa_typed_struct_cc_proto",
    deps = ["udpa_typed_struct_proto"],
)

grpc_internal_proto_library(
    name = "cookie_proto",
    srcs = ["cookie.proto"],
    deps = [
        "@com_google_protobuf//:duration_proto",
    ],
)

grpc_cc_proto_library(
    name = "cookie_cc_proto",
    deps = ["cookie_proto"],
)

grpc_internal_proto_library(
    name = "stateful_session_cookie_proto",
    srcs = ["stateful_session_cookie.proto"],
    deps = [
        "cookie_proto",
    ],
)

grpc_cc_proto_library(
    name = "stateful_session_cookie_cc_proto",
    deps = ["stateful_session_cookie_proto"],
)

grpc_internal_proto_library(
    name = "stateful_session_proto",
    srcs = ["stateful_session.proto"],
    deps = [
        "extension_proto",
    ],
)

grpc_cc_proto_library(
    name = "stateful_session_cc_proto",
    deps = ["stateful_session_proto"],
)

py_proto_library(
    name = "csds_py_pb2",
    deps = [":csds_proto"],
)

py_grpc_library(
    name = "csds_py_pb2_grpc",
    srcs = [":csds_proto"],
    deps = [":csds_py_pb2"],
)

py_proto_library(
    name = "config_dump_py_pb2",
    deps = [":config_dump_proto"],
)

py_proto_library(
    name = "base_py_pb2",
    deps = [":base_proto"],
)

py_proto_library(
    name = "percent_py_pb2",
    deps = [":percent_proto"],
)
