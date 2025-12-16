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

load("//bazel:grpc_build_system.bzl", "grpc_cc_proto_library", "grpc_package")

licenses(["notice"])

grpc_package(
    name = "src/proto/grpc/testing/xds",
    features = [
        "layering_check",
        "parse_headers",
    ],
    visibility = "public",
)

grpc_cc_proto_library(
    name = "cluster_cc_proto",
    deps = ["@envoy_api//envoy/config/cluster/v3:pkg"],
)

grpc_cc_proto_library(
    name = "endpoint_cc_proto",
    deps = ["@envoy_api//envoy/config/endpoint/v3:pkg"],
)

grpc_cc_proto_library(
    name = "listener_cc_proto",
    deps = ["@envoy_api//envoy/config/listener/v3:pkg"],
)

grpc_cc_proto_library(
    name = "route_cc_proto",
    deps = ["@envoy_api//envoy/config/route/v3:pkg"],
)

grpc_cc_proto_library(
    name = "discovery_cc_proto",
    deps = ["@envoy_api//envoy/service/discovery/v3:pkg"],
)

grpc_cc_proto_library(
    name = "http_connection_manager_cc_proto",
    deps = ["@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg"],
)

grpc_cc_proto_library(
    name = "router_cc_proto",
    deps = ["@envoy_api//envoy/extensions/filters/http/router/v3:pkg"],
)

grpc_cc_proto_library(
    name = "config_rbac_cc_proto",
    deps = ["@envoy_api//envoy/config/rbac/v3:pkg"],
)

grpc_cc_proto_library(
    name = "http_rbac_cc_proto",
    deps = ["@envoy_api//envoy/extensions/filters/http/rbac/v3:pkg"],
)

grpc_cc_proto_library(
    name = "orca_load_report_cc_proto",
    deps = ["@com_github_cncf_xds//xds/data/orca/v3:pkg"],
)

grpc_cc_proto_library(
    name = "aggregate_cc_proto",
    deps = ["@envoy_api//envoy/extensions/clusters/aggregate/v3:pkg"],
)

grpc_cc_proto_library(
    name = "tls_cc_proto",
    deps = ["@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg"],
)

grpc_cc_proto_library(
    name = "type_cc_proto",
    deps = ["@envoy_api//envoy/type/v3:pkg"],
)

grpc_cc_proto_library(
    name = "config_core_cc_proto",
    deps = ["@envoy_api//envoy/config/core/v3:pkg"],
)

grpc_cc_proto_library(
    name = "http_11_proxy_cc_proto",
    deps = ["@envoy_api//envoy/extensions/transport_sockets/http_11_proxy/v3:pkg"],
)

grpc_cc_proto_library(
    name = "pick_first_cc_proto",
    deps = ["@envoy_api//envoy/extensions/load_balancing_policies/pick_first/v3:pkg"],
)

grpc_cc_proto_library(
    name = "load_stats_cc_proto",
    deps = ["@envoy_api//envoy/service/load_stats/v3:pkg"],
)

grpc_cc_proto_library(
    name = "status_cc_proto",
    deps = ["@envoy_api//envoy/service/status/v3:pkg"],
)

grpc_cc_proto_library(
    name = "fault_cc_proto",
    deps = ["@envoy_api//envoy/extensions/filters/http/fault/v3:pkg"],
)

grpc_cc_proto_library(
    name = "gcp_authn_cc_proto",
    deps = ["@envoy_api//envoy/extensions/filters/http/gcp_authn/v3:pkg"],
)

grpc_cc_proto_library(
    name = "client_side_weighted_round_robin_cc_proto",
    deps = ["@envoy_api//envoy/extensions/load_balancing_policies/client_side_weighted_round_robin/v3:pkg"],
)

grpc_cc_proto_library(
    name = "wrr_locality_cc_proto",
    deps = ["@envoy_api//envoy/extensions/load_balancing_policies/wrr_locality/v3:pkg"],
)

grpc_cc_proto_library(
    name = "stateful_session_cc_proto",
    deps = ["@envoy_api//envoy/extensions/filters/http/stateful_session/v3:pkg"],
)

grpc_cc_proto_library(
    name = "stateful_session_cookie_cc_proto",
    deps = ["@envoy_api//envoy/extensions/http/stateful_session/cookie/v3:pkg"],
)

grpc_cc_proto_library(
    name = "typed_struct_cc_proto",
    deps = ["@com_github_cncf_xds//xds/type/v3:pkg"],
)

alias(
    name = "discovery_cc_grpc",
    actual = "@envoy_api//envoy/service/discovery/v3:pkg_cc_grpc",
)

alias(
    name = "load_stats_cc_grpc",
    actual = "@envoy_api//envoy/service/load_stats/v3:pkg_cc_grpc",
)

alias(
    name = "status_cc_grpc",
    actual = "@envoy_api//envoy/service/status/v3:pkg_cc_grpc",
)
