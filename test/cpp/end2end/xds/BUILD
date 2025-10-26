# Copyright 2017 gRPC authors.
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

load("//bazel:grpc_build_system.bzl", "grpc_cc_library", "grpc_cc_test", "grpc_package")

licenses(["notice"])

grpc_package(
    name = "test/cpp/end2end/xds",
    visibility = "public",
)  # Allows external users to implement end2end tests.

grpc_cc_library(
    name = "xds_server",
    srcs = ["xds_server.cc"],
    hdrs = ["xds_server.h"],
    external_deps = [
        "absl/log:check",
        "absl/log:log",
        "absl/container:flat_hash_map",
        "absl/container:flat_hash_set",
    ],
    visibility = ["//bazel:xds_end2end_test_utils"],
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//:parse_address",
        "//src/core:grpc_check",
        "//src/core:sync",
        "//test/core/test_util:grpc_test_util",
        "//test/cpp/end2end:counted_service",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/listener/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_grpc",
        "@envoy_api//envoy/service/load_stats/v3:pkg_cc_grpc",
    ],
)

grpc_cc_library(
    name = "xds_utils",
    srcs = ["xds_utils.cc"],
    hdrs = ["xds_utils.h"],
    external_deps = [
        "absl/strings",
        "absl/memory",
        "absl/strings:str_format",
    ],
    visibility = ["//bazel:xds_end2end_test_utils"],
    deps = [
        ":xds_server",
        "//:gpr",
        "//:grpc++",
        "//:grpc_http_filters",
        "//:server",
        "//:xds_client",
        "//src/core:env",
        "//src/core:grpc_xds_client",
        "//test/core/test_util:grpc_test_util_base",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/listener/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/router/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
    ],
)

grpc_cc_library(
    name = "xds_end2end_test_lib",
    testonly = True,
    srcs = ["xds_end2end_test_lib.cc"],
    hdrs = ["xds_end2end_test_lib.h"],
    external_deps = [
        "absl/log",
        "absl/log:check",
        "gtest",
        "absl/status:statusor",
        "absl/memory",
        "absl/strings",
        "absl/strings:str_format",
    ],
    deps = [
        ":xds_server",
        ":xds_utils",
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//:grpc++_xds_server",
        "//:grpc_http_filters",
        "//:grpc_resolver_fake",
        "//:grpcpp_backend_metric_recorder",
        "//:grpcpp_call_metric_recorder",
        "//:server",
        "//:tsi_ssl_credentials",
        "//:xds_client",
        "//src/core:default_event_engine",
        "//src/core:env",
        "//src/core:grpc_check",
        "//src/core:grpc_fake_credentials",
        "//src/core:grpc_xds_client",
        "//src/proto/grpc/testing:echo_cc_grpc",
        "//src/proto/grpc/testing:echo_cc_proto",
        "//src/proto/grpc/testing:echo_messages_cc_proto",
        "//src/proto/grpc/testing/duplicate:echo_duplicate_cc_grpc",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
        "//test/cpp/end2end:counted_service",
        "//test/cpp/end2end:test_service_impl",
        "//test/cpp/util:test_util",
        "//test/cpp/util:tls_test_utils",
        "@com_github_cncf_xds//xds/data/orca/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/listener/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/rbac/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/rbac/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/router/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_grpc",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/load_stats/v3:pkg_cc_grpc",
    ],
)

grpc_cc_test(
    name = "xds_security_end2end_test",
    size = "large",
    srcs = ["xds_security_end2end_test.cc"],
    data = [
        "//src/core/tsi/test_creds:badclient.key",
        "//src/core/tsi/test_creds:badclient.pem",
        "//src/core/tsi/test_creds:ca.pem",
        "//src/core/tsi/test_creds:client.key",
        "//src/core/tsi/test_creds:client.pem",
        "//src/core/tsi/test_creds:server1.key",
        "//src/core/tsi/test_creds:server1.pem",
    ],
    external_deps = [
        "absl/log:check",
        "absl/functional:function_ref",
        "absl/log",
        "absl/strings",
        "absl/strings:str_format",
        "absl/time",
        "gtest",
    ],
    flaky = True,  # TODO(b/144705388)
    linkstatic = True,  # Fixes dyld error on MacOS
    shard_count = 25,
    tags = [
        "no_test_ios",
        "no_windows",
        "xds_end2end_test",
    ],  # TODO(jtattermusch): fix test on windows
    deps = [
        ":xds_end2end_test_lib",
        "//:config",
        "//:config_vars",
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//:grpc++_base",
        "//:grpc++_xds_server",
        "//:grpc_base",
        "//:grpc_http_filters",
        "//:ref_counted_ptr",
        "//src/core:certificate_provider_registry",
        "//src/core:env",
        "//src/core:grpc_audit_logging",
        "//src/core:grpc_check",
        "//src/core:grpc_fake_credentials",
        "//src/core:grpc_tls_credentials",
        "//src/core:sync",
        "//src/proto/grpc/testing:echo_cc_grpc",
        "//src/proto/grpc/testing:echo_messages_cc_proto",
        "//test/core/test_util:audit_logging_utils",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
        "//test/core/test_util:scoped_env_var",
        "//test/cpp/util:test_config",
        "//test/cpp/util:test_util",
        "//test/cpp/util:tls_test_utils",
        "@com_github_cncf_xds//xds/type/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/listener/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/clusters/aggregate/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/rbac/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/router/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/v3:pkg_cc_proto",
    ],
)

grpc_cc_test(
    name = "xds_security_spiffe_end2end_test",
    size = "large",
    srcs = ["xds_security_spiffe_end2end_test.cc"],
    data = [
        "//src/core/tsi/test_creds:badclient.key",
        "//src/core/tsi/test_creds:badclient.pem",
        "//src/core/tsi/test_creds:ca.pem",
        "//src/core/tsi/test_creds:client.key",
        "//src/core/tsi/test_creds:client.pem",
        "//src/core/tsi/test_creds:server1.key",
        "//src/core/tsi/test_creds:server1.pem",
        "//test/core/tsi/test_creds/spiffe_end2end:ca.pem",
        "//test/core/tsi/test_creds/spiffe_end2end:client.key",
        "//test/core/tsi/test_creds/spiffe_end2end:client_spiffe.pem",
        "//test/core/tsi/test_creds/spiffe_end2end:client_spiffebundle.json",
        "//test/core/tsi/test_creds/spiffe_end2end:intermediate.cnf",
        "//test/core/tsi/test_creds/spiffe_end2end:intermediate_ca.key",
        "//test/core/tsi/test_creds/spiffe_end2end:intermediate_ca.pem",
        "//test/core/tsi/test_creds/spiffe_end2end:intermediate_gen.sh",
        "//test/core/tsi/test_creds/spiffe_end2end:leaf_and_intermediate_chain.pem",
        "//test/core/tsi/test_creds/spiffe_end2end:leaf_signed_by_intermediate.key",
        "//test/core/tsi/test_creds/spiffe_end2end:leaf_signed_by_intermediate.pem",
        "//test/core/tsi/test_creds/spiffe_end2end:server.key",
        "//test/core/tsi/test_creds/spiffe_end2end:server_spiffe.pem",
        "//test/core/tsi/test_creds/spiffe_end2end:server_spiffebundle.json",
    ],
    external_deps = [
        "absl/log:check",
        "absl/functional:function_ref",
        "absl/log",
        "absl/strings",
        "absl/strings:str_format",
        "absl/time",
        "gtest",
    ],
    # flaky = True,  # TODO(b/144705388)
    linkstatic = True,  # Fixes dyld error on MacOS
    # shard_count = 25,
    tags = [
        "no_test_ios",
        "no_windows",
        "xds_end2end_test",
    ],  # TODO(jtattermusch): fix test on windows
    deps = [
        ":xds_end2end_test_lib",
        "//:config",
        "//:config_vars",
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//:grpc++_base",
        "//:grpc++_xds_server",
        "//:grpc_base",
        "//:grpc_http_filters",
        "//:grpc_security_base",
        "//:ref_counted_ptr",
        "//src/core:certificate_provider_registry",
        "//src/core:env",
        "//src/core:grpc_audit_logging",
        "//src/core:grpc_fake_credentials",
        "//src/core:grpc_tls_credentials",
        "//src/core:sync",
        "//src/proto/grpc/testing:echo_cc_grpc",
        "//src/proto/grpc/testing:echo_messages_cc_proto",
        "//test/core/test_util:audit_logging_utils",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
        "//test/core/test_util:scoped_env_var",
        "//test/cpp/util:test_config",
        "//test/cpp/util:test_util",
        "//test/cpp/util:tls_test_utils",
        "@com_github_cncf_xds//xds/type/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/listener/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/clusters/aggregate/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/rbac/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/router/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg_cc_proto",
        "@envoy_api//envoy/type/v3:pkg_cc_proto",
    ],
)

grpc_cc_test(
    name = "xds_cluster_end2end_test",
    size = "large",
    srcs = ["xds_cluster_end2end_test.cc"],
    external_deps = [
        "absl/log",
        "absl/strings",
        "gtest",
    ],
    flaky = True,  # TODO(b/144705388)
    linkstatic = True,  # Fixes dyld error on MacOS
    shard_count = 50,
    tags = [
        "no_test_ios",
        "no_windows",
        "xds_end2end_test",
    ],  # TODO(jtattermusch): fix test on windows
    deps = [
        ":xds_end2end_test_lib",
        "//:call_tracer",
        "//:config_vars",
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//:grpc_base",
        "//:sockaddr_utils",
        "//src/core:client_channel_backup_poller",
        "//test/core/test_util:fake_stats_plugin",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:scoped_env_var",
        "//test/cpp/end2end:connection_attempt_injector",
        "@com_github_cncf_xds//xds/data/orca/v3:pkg_cc_proto",
    ],
)

grpc_cc_test(
    name = "xds_cluster_type_end2end_test",
    size = "large",
    srcs = ["xds_cluster_type_end2end_test.cc"],
    external_deps = [
        "absl/log",
        "absl/log:check",
        "absl/status:statusor",
        "absl/strings",
        "gtest",
    ],
    flaky = True,  # TODO(b/144705388)
    linkstatic = True,  # Fixes dyld error on MacOS
    tags = [
        "no_test_ios",
        "no_windows",
        "xds_end2end_test",
    ],  # TODO(jtattermusch): fix test on windows
    deps = [
        ":xds_end2end_test_lib",
        "//:config_vars",
        "//:endpoint_addresses",
        "//:event_engine_base_hdrs",
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//:grpc_resolver_fake",
        "//:sockaddr_utils",
        "//src/core:client_channel_backup_poller",
        "//src/core:env",
        "//src/core:grpc_check",
        "//src/core:grpc_lb_xds_channel_args",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
        "//test/core/test_util:scoped_env_var",
        "//test/cpp/end2end:connection_attempt_injector",
        "@envoy_api//envoy/extensions/clusters/aggregate/v3:pkg_cc_proto",
    ],
)

grpc_cc_test(
    name = "xds_core_end2end_test",
    size = "large",
    srcs = ["xds_core_end2end_test.cc"],
    data = [
        "//src/core/tsi/test_creds:ca.pem",
        "//src/core/tsi/test_creds:server1.key",
        "//src/core/tsi/test_creds:server1.pem",
    ],
    external_deps = [
        "gtest",
        "absl/log",
        "absl/strings",
    ],
    flaky = True,
    linkstatic = True,  # Fixes dyld error on MacOS
    shard_count = 15,
    tags = [
        "no_test_ios",
        "no_windows",
        "xds_end2end_test",
    ],  # TODO(jtattermusch): fix test on windows
    deps = [
        ":xds_end2end_test_lib",
        ":xds_server",
        "//:config_vars",
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//src/core:client_channel_backup_poller",
        "//test/core/test_util:fake_stats_plugin",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
        "//test/core/test_util:scoped_env_var",
        "@envoy_api//envoy/config/listener/v3:pkg_cc_proto",
    ],
)

grpc_cc_test(
    name = "xds_csds_end2end_test",
    size = "large",
    srcs = ["xds_csds_end2end_test.cc"],
    external_deps = [
        "gtest",
        "absl/log",
        "absl/memory",
        "absl/strings",
    ],
    linkstatic = True,  # Fixes dyld error on MacOS
    tags = [
        "no_test_ios",
        "no_windows",
        "xds_end2end_test",
    ],  # TODO(jtattermusch): fix test on windows
    deps = [
        ":xds_end2end_test_lib",
        "//:config_vars",
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//:grpc++_base",
        "//:grpcpp_csds",
        "//src/core:client_channel_backup_poller",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
        "//test/cpp/util:test_util",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/listener/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
    ],
)

grpc_cc_test(
    name = "xds_fault_injection_end2end_test",
    size = "large",
    srcs = ["xds_fault_injection_end2end_test.cc"],
    external_deps = [
        "gtest",
    ],
    flaky = True,
    linkstatic = True,  # Fixes dyld error on MacOS
    shard_count = 5,
    tags = [
        "no_mac",
        "no_test_ios",
        "no_windows",
        "xds_end2end_test",
    ],  # TODO(jtattermusch): fix test on windows
    deps = [
        ":xds_end2end_test_lib",
        "//:config_vars",
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//src/core:client_channel_backup_poller",
        "//test/core/test_util:grpc_test_util",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/fault/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/router/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
    ],
)

grpc_cc_test(
    name = "xds_gcp_authn_end2end_test",
    size = "large",
    srcs = ["xds_gcp_authn_end2end_test.cc"],
    data = [
        "//src/core/tsi/test_creds:badclient.key",
        "//src/core/tsi/test_creds:badclient.pem",
        "//src/core/tsi/test_creds:ca.pem",
        "//src/core/tsi/test_creds:client.key",
        "//src/core/tsi/test_creds:client.pem",
        "//src/core/tsi/test_creds:server1.key",
        "//src/core/tsi/test_creds:server1.pem",
    ],
    external_deps = [
        "gtest",
    ],
    flaky = True,
    linkstatic = True,  # Fixes dyld error on MacOS
    tags = [
        "no_test_ios",
        "no_windows",
        "xds_end2end_test",
    ],  # TODO(jtattermusch): fix test on windows
    deps = [
        ":xds_end2end_test_lib",
        "//:config_vars",
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//:httpcli",
        "//src/core:client_channel_backup_poller",
        "//test/core/test_util:grpc_test_util",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/gcp_authn/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/router/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
    ],
)

grpc_cc_test(
    name = "xds_outlier_detection_end2end_test",
    size = "large",
    srcs = ["xds_outlier_detection_end2end_test.cc"],
    external_deps = [
        "absl/log:check",
        "gtest",
    ],
    flaky = True,
    linkstatic = True,  # Fixes dyld error on MacOS
    tags = [
        "no_test_ios",
        "no_windows",
        "xds_end2end_test",
    ],  # TODO(jtattermusch): fix test on windows
    deps = [
        ":xds_end2end_test_lib",
        "//:config_vars",
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//src/core:client_channel_backup_poller",
        "//src/core:grpc_check",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/fault/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/router/v3:pkg_cc_proto",
    ],
)

grpc_cc_test(
    name = "xds_wrr_end2end_test",
    size = "large",
    srcs = ["xds_wrr_end2end_test.cc"],
    external_deps = [
        "gtest",
        "absl/log",
        "absl/strings",
        "absl/strings:str_format",
    ],
    linkstatic = True,  # Fixes dyld error on MacOS
    tags = [
        "no_test_ios",
        "no_windows",
        "xds_end2end_test",
    ],  # TODO(jtattermusch): fix test on windows
    deps = [
        ":xds_end2end_test_lib",
        "//:config_vars",
        "//:event_engine_base_hdrs",
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//:grpcpp_backend_metric_recorder",
        "//src/core:client_channel_backup_poller",
        "//test/core/test_util:fake_stats_plugin",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:scoped_env_var",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/load_balancing_policies/client_side_weighted_round_robin/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/load_balancing_policies/wrr_locality/v3:pkg_cc_proto",
    ],
)

grpc_cc_test(
    name = "xds_ring_hash_end2end_test",
    size = "large",
    srcs = ["xds_ring_hash_end2end_test.cc"],
    external_deps = [
        "absl/log:check",
        "absl/log:log",
        "absl/strings",
        "absl/strings:str_format",
        "gtest",
    ],
    flaky = True,  # TODO(b/144705388)
    linkstatic = True,  # Fixes dyld error on MacOS
    shard_count = 10,
    tags = [
        "no_test_ios",
        "no_windows",
        "xds_end2end_test",
    ],  # TODO(jtattermusch): fix test on windows
    deps = [
        ":xds_end2end_test_lib",
        "//:config_vars",
        "//:event_engine_base_hdrs",
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//:grpc_resolver_fake",
        "//:sockaddr_utils",
        "//src/core:client_channel_backup_poller",
        "//src/core:env",
        "//src/core:grpc_check",
        "//src/core:grpc_lb_xds_channel_args",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
        "//test/core/test_util:scoped_env_var",
        "//test/cpp/end2end:connection_attempt_injector",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/listener/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/clusters/aggregate/v3:pkg_cc_proto",
    ],
)

grpc_cc_test(
    name = "xds_rls_end2end_test",
    size = "large",
    srcs = ["xds_rls_end2end_test.cc"],
    external_deps = [
        "gtest",
        "absl/memory",
        "absl/strings",
    ],
    linkstatic = True,  # Fixes dyld error on MacOS
    tags = [
        "no_test_ios",
        "no_windows",
        "xds_end2end_test",
    ],  # TODO(jtattermusch): fix test on windows
    deps = [
        ":xds_end2end_test_lib",
        "//:config_vars",
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//src/core:client_channel_backup_poller",
        "//src/core:env",
        "//src/proto/grpc/lookup/v1:rls_cc_proto",
        "//src/proto/grpc/lookup/v1:rls_config_cc_proto",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:scoped_env_var",
        "//test/cpp/end2end:rls_server",
    ],
)

grpc_cc_test(
    name = "xds_routing_end2end_test",
    size = "large",
    srcs = ["xds_routing_end2end_test.cc"],
    external_deps = [
        "absl/log",
        "gtest",
    ],
    flaky = True,  # TODO(b/144705388)
    linkstatic = True,  # Fixes dyld error on MacOS
    shard_count = 10,
    tags = [
        "no_test_ios",
        "no_windows",
        "xds_end2end_test",
    ],  # TODO(jtattermusch): fix test on windows
    deps = [
        ":xds_end2end_test_lib",
        "//:config_vars",
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//src/core:client_channel_backup_poller",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:scoped_env_var",
        "@envoy_api//envoy/extensions/filters/http/fault/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/router/v3:pkg_cc_proto",
    ],
)

grpc_cc_test(
    name = "xds_credentials_end2end_test",
    srcs = ["xds_credentials_end2end_test.cc"],
    external_deps = [
        "gtest",
    ],
    tags = [
        "no_test_ios",
        "xds_end2end_test",
    ],
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//src/proto/grpc/testing:echo_cc_grpc",
        "//src/proto/grpc/testing:echo_messages_cc_proto",
        "//test/core/test_util:grpc_test_util",
        "//test/cpp/end2end:test_service_impl",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_test(
    name = "xds_override_host_end2end_test",
    size = "large",
    srcs = ["xds_override_host_end2end_test.cc"],
    external_deps = [
        "absl/log:log",
        "absl/strings",
        "absl/strings:str_format",
        "gtest",
    ],
    linkstatic = True,  # Fixes dyld error on MacOS
    tags = [
        "no_test_ios",
        "no_windows",
        "xds_end2end_test",
    ],  # TODO(jtattermusch): fix test on windows
    deps = [
        ":xds_end2end_test_lib",
        "//:config_vars",
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//src/core:time",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
        "//test/core/test_util:scoped_env_var",
        "@envoy_api//envoy/extensions/filters/http/stateful_session/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/http/stateful_session/cookie/v3:pkg_cc_proto",
    ],
)

grpc_cc_test(
    name = "xds_pick_first_end2end_test",
    size = "large",
    srcs = ["xds_pick_first_end2end_test.cc"],
    external_deps = [
        "gtest",
        "absl/strings",
    ],
    linkstatic = True,  # Fixes dyld error on MacOS
    tags = [
        "no_test_ios",
        "no_windows",
        "xds_end2end_test",
    ],  # TODO(jtattermusch): fix test on windows
    deps = [
        ":xds_end2end_test_lib",
        "//:config_vars",
        "//:event_engine_base_hdrs",
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//:grpc++_base",
        "//:grpc_resolver_fake",
        "//:sockaddr_utils",
        "//src/core:client_channel_backup_poller",
        "//src/core:env",
        "//src/core:grpc_lb_xds_channel_args",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
        "//test/core/test_util:scoped_env_var",
        "//test/cpp/end2end:connection_attempt_injector",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/clusters/aggregate/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/router/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/load_balancing_policies/pick_first/v3:pkg_cc_proto",
    ],
)

grpc_cc_test(
    name = "xds_fallback_end2end_test",
    srcs = ["xds_fallback_end2end_test.cc"],
    external_deps = [
        "gtest",
        "absl/cleanup",
        "absl/strings:str_format",
        "absl/strings",
    ],
    linkstatic = True,  # Fixes dyld error on MacOS
    tags = [
        "no_test_ios",
        "no_windows",
        "xds_end2end_test",
    ],  # TODO(jtattermusch): fix test on windows
    deps = [
        ":xds_end2end_test_lib",
        ":xds_utils",
        "//:config_vars",
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//:grpc++_base",
        "//src/proto/grpc/testing:echo_cc_proto",
        "//src/proto/grpc/testing:echo_messages_cc_proto",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
        "//test/core/test_util:scoped_env_var",
    ],
)

grpc_cc_test(
    name = "xds_http_connect_end2end_test",
    srcs = ["xds_http_connect_end2end_test.cc"],
    external_deps = [
        "gtest",
        "absl/log",
        "absl/strings",
        "absl/strings:str_format",
    ],
    linkstatic = True,  # Fixes dyld error on MacOS
    tags = [
        "no_test_ios",
        "no_windows",
        "xds_end2end_test",
    ],  # TODO(jtattermusch): fix test on windows
    deps = [
        ":xds_end2end_test_lib",
        "//:config_vars",
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//src/core:client_channel_backup_poller",
        "//test/core/end2end:http_proxy",
        "//test/core/test_util:grpc_test_util_base",
        "//test/core/test_util:scoped_env_var",
        "@envoy_api//envoy/config/core/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/http_11_proxy/v3:pkg_cc_proto",
    ],
)

grpc_cc_test(
    name = "xds_enabled_server_end2end_test",
    size = "large",
    srcs = ["xds_enabled_server_end2end_test.cc"],
    external_deps = [
        "gtest",
        "absl/strings",
        "absl/time",
    ],
    flaky = True,  # TODO(b/144705388)
    linkstatic = True,  # Fixes dyld error on MacOS
    tags = [
        "no_test_ios",
        "no_windows",
        "xds_end2end_test",
    ],  # TODO(jtattermusch): fix test on windows
    deps = [
        ":xds_end2end_test_lib",
        "//:config_vars",
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//:grpc++_base",
        "//:grpc++_xds_server",
        "//:grpc_base",
        "//src/core:env",
        "//src/core:time",
        "//src/proto/grpc/testing:echo_cc_proto",
        "//test/core/test_util:grpc_test_util",
        "//test/core/test_util:grpc_test_util_base",
        "//test/core/test_util:scoped_env_var",
        "@envoy_api//envoy/config/listener/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
    ],
)
