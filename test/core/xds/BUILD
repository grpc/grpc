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

load(
    "//bazel:grpc_build_system.bzl",
    "grpc_cc_library",
    "grpc_cc_test",
    "grpc_package",
)
load("//test/core/test_util:grpc_fuzzer.bzl", "grpc_proto_fuzzer")

grpc_package(name = "test/core/xds")

licenses(["notice"])

grpc_cc_test(
    name = "xds_bootstrap_test",
    srcs = ["xds_bootstrap_test.cc"],
    external_deps = [
        "gtest",
    ],
    language = "C++",
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:gpr",
        "//src/core:grpc_xds_client",
        "//src/core:xds_server_grpc",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "certificate_provider_store_test",
    srcs = ["certificate_provider_store_test.cc"],
    external_deps = [
        "absl/log:check",
        "gtest",
    ],
    language = "C++",
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "file_watcher_certificate_provider_factory_test",
    srcs = ["file_watcher_certificate_provider_factory_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "xds_certificate_provider_test",
    srcs = ["xds_certificate_provider_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "xds_lb_policy_registry_test",
    srcs = ["xds_lb_policy_registry_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:scoped_env_var",
        "//test/cpp/util:grpc_cli_utils",
        "@com_github_cncf_xds//xds/type/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/load_balancing_policies/client_side_weighted_round_robin/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/load_balancing_policies/pick_first/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/load_balancing_policies/ring_hash/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/load_balancing_policies/round_robin/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/load_balancing_policies/wrr_locality/v3:pkg_cc_proto",
    ],
)

grpc_cc_test(
    name = "xds_audit_logger_registry_test",
    srcs = ["xds_audit_logger_registry_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:gpr",
        "//:grpc",
        "//src/core:grpc_audit_logging",
        "//test/core/test_util:grpc_test_util",
        "//test/cpp/util:grpc_cli_utils",
        "@com_github_cncf_xds//xds/type/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/rbac/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/rbac/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/rbac/audit_loggers/stream/v3:pkg_cc_proto",
    ],
)

grpc_cc_library(
    name = "xds_transport_fake",
    testonly = True,
    srcs = ["xds_transport_fake.cc"],
    hdrs = ["xds_transport_fake.h"],
    external_deps = [
        "absl/log:check",
        "absl/log:log",
        "absl/strings",
        "absl/types:optional",
    ],
    language = "C++",
    deps = [
        "//:orphanable",
        "//:ref_counted_ptr",
        "//:xds_client",
        "//test/core/event_engine/fuzzing_event_engine",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_library(
    name = "xds_client_test_peer",
    hdrs = ["xds_client_test_peer.h"],
    external_deps = [
        "absl/functional:function_ref",
    ],
    language = "C++",
    deps = [
        "//:xds_client",
    ],
)

grpc_cc_test(
    name = "xds_client_test",
    srcs = ["xds_client_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    uses_event_engine = True,
    uses_polling = False,
    deps = [
        ":xds_client_test_peer",
        ":xds_transport_fake",
        "//:grpc++_codegen_proto",
        "//:grpc++_config_proto",
        "//:xds_client",
        "//test/core/event_engine:event_engine_test_utils",
        "//test/core/event_engine/fuzzing_event_engine",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:scoped_env_var",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
    ],
)

# TODO(roth): Convert this to use the real xDS protos instead of our
# local copies.
grpc_proto_fuzzer(
    name = "xds_client_fuzzer",
    srcs = ["xds_client_fuzzer.cc"],
    corpus = "xds_client_corpora",
    external_deps = ["absl/log:log"],
    language = "C++",
    proto = "xds_client_fuzzer.proto",
    proto_deps = [
        "//src/proto/grpc/testing/xds/v3:discovery_proto",
        "//test/core/event_engine/fuzzing_event_engine:fuzzing_event_engine_proto",
    ],
    tags = ["no_windows"],
    uses_event_engine = True,
    uses_polling = False,
    deps = [
        ":xds_client_test_peer",
        ":xds_transport_fake",
        "//src/core:grpc_xds_client",
        "//test/core/test_util:grpc_test_util",
        # These proto deps are needed to ensure that we can read these
        # resource types out of the google.protobuf.Any fields in the
        # textproto files in the corpora.
        "//src/proto/grpc/testing/xds/v3:listener_proto",
        "//src/proto/grpc/testing/xds/v3:route_proto",
        "//src/proto/grpc/testing/xds/v3:cluster_proto",
        "//src/proto/grpc/testing/xds/v3:endpoint_proto",
        "//src/proto/grpc/testing/xds/v3:http_connection_manager_proto",
        "//src/proto/grpc/testing/xds/v3:router_proto",
        "//test/core/event_engine/fuzzing_event_engine",
        "//test/core/event_engine:event_engine_test_utils",
    ],
)

grpc_cc_test(
    name = "xds_common_types_test",
    srcs = ["xds_common_types_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:gpr",
        "//:grpc",
        "//src/core:grpc_xds_client",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:scoped_env_var",
        "//test/cpp/util:grpc_cli_utils",
        "@com_github_cncf_xds//udpa/type/v1:pkg_cc_proto",
        "@com_github_cncf_xds//xds/type/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/matcher/v3:pkg_cc_proto",
    ],
)

grpc_cc_test(
    name = "xds_metadata_test",
    srcs = ["xds_metadata_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:gpr",
        "//:grpc",
        "//src/core:grpc_xds_client",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:scoped_env_var",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/gcp_authn/v3:pkg_cc_proto",
    ],
)

grpc_cc_test(
    name = "xds_http_filters_test",
    srcs = ["xds_http_filters_test.cc"],
    external_deps = [
        "absl/log:check",
        "gtest",
    ],
    language = "C++",
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:scoped_env_var",
        "//test/cpp/util:grpc_cli_utils",
        "@com_github_cncf_xds//xds/type/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/rbac/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/common/fault/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/fault/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/gcp_authn/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/rbac/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/router/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/stateful_session/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/http/stateful_session/cookie/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/http/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/v3:pkg_cc_proto",
    ],
)

grpc_cc_test(
    name = "xds_listener_resource_type_test",
    srcs = ["xds_listener_resource_type_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc++_codegen_proto",
        "//src/core:grpc_xds_client",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:scoped_env_var",
        "//test/cpp/util:grpc_cli_utils",
        "@com_github_cncf_xds//xds/type/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/listener/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/fault/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/rbac/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/router/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg_cc_proto",
    ],
)

grpc_cc_test(
    name = "xds_route_config_resource_type_test",
    srcs = ["xds_route_config_resource_type_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc++_codegen_proto",
        "//src/core:grpc_xds_client",
        "//src/proto/grpc/lookup/v1:rls_config_proto",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:scoped_env_var",
        "//test/cpp/util:grpc_cli_utils",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/fault/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/rbac/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/v3:pkg_cc_proto",
    ],
)

grpc_cc_test(
    name = "xds_cluster_resource_type_test",
    srcs = ["xds_cluster_resource_type_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc++_codegen_proto",
        "//src/core:grpc_xds_client",
        "//src/core:xds_health_status",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:scoped_env_var",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/clusters/aggregate/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/gcp_authn/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/load_balancing_policies/round_robin/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/load_balancing_policies/wrr_locality/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/http_11_proxy/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/upstreams/http/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/v3:pkg_cc_proto",
    ],
)

grpc_cc_test(
    name = "xds_endpoint_resource_type_test",
    srcs = ["xds_endpoint_resource_type_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc++_codegen_proto",
        "//src/core:channel_args",
        "//src/core:grpc_xds_client",
        "//src/core:xds_health_status",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:scoped_env_var",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
    ],
)
