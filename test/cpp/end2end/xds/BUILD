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
    testonly = True,
    srcs = ["xds_server.cc"],
    hdrs = ["xds_server.h"],
    external_deps = [
        "gtest",
    ],
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//src/proto/grpc/testing/xds/v3:ads_proto",
        "//src/proto/grpc/testing/xds/v3:cluster_proto",
        "//src/proto/grpc/testing/xds/v3:discovery_proto",
        "//src/proto/grpc/testing/xds/v3:endpoint_proto",
        "//src/proto/grpc/testing/xds/v3:listener_proto",
        "//src/proto/grpc/testing/xds/v3:lrs_proto",
        "//src/proto/grpc/testing/xds/v3:route_proto",
        "//test/core/util:grpc_test_util",
        "//test/cpp/end2end:counted_service",
    ],
)

grpc_cc_library(
    name = "xds_end2end_test_lib",
    testonly = True,
    srcs = ["xds_end2end_test_lib.cc"],
    hdrs = ["xds_end2end_test_lib.h"],
    external_deps = [
        "gtest",
    ],
    deps = [
        ":xds_server",
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//:grpc_resolver_fake",
        "//src/proto/grpc/testing:echo_messages_proto",
        "//src/proto/grpc/testing:echo_proto",
        "//src/proto/grpc/testing/duplicate:echo_duplicate_proto",
        "//src/proto/grpc/testing/xds/v3:ads_proto",
        "//src/proto/grpc/testing/xds/v3:cluster_proto",
        "//src/proto/grpc/testing/xds/v3:discovery_proto",
        "//src/proto/grpc/testing/xds/v3:endpoint_proto",
        "//src/proto/grpc/testing/xds/v3:http_connection_manager_proto",
        "//src/proto/grpc/testing/xds/v3:http_filter_rbac_proto",
        "//src/proto/grpc/testing/xds/v3:listener_proto",
        "//src/proto/grpc/testing/xds/v3:lrs_proto",
        "//src/proto/grpc/testing/xds/v3:route_proto",
        "//src/proto/grpc/testing/xds/v3:router_proto",
        "//test/core/util:grpc_test_util",
        "//test/cpp/end2end:counted_service",
        "//test/cpp/end2end:test_service_impl",
        "//test/cpp/util:tls_test_utils",
    ],
)

grpc_cc_test(
    name = "xds_end2end_test",
    size = "large",
    srcs = ["xds_end2end_test.cc"],
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
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//:grpc_resolver_fake",
        "//src/core:channel_args",
        "//src/proto/grpc/testing:echo_messages_proto",
        "//src/proto/grpc/testing:echo_proto",
        "//src/proto/grpc/testing/duplicate:echo_duplicate_proto",
        "//src/proto/grpc/testing/xds/v3:aggregate_cluster_proto",
        "//src/proto/grpc/testing/xds/v3:cluster_proto",
        "//src/proto/grpc/testing/xds/v3:endpoint_proto",
        "//src/proto/grpc/testing/xds/v3:fault_common_proto",
        "//src/proto/grpc/testing/xds/v3:fault_proto",
        "//src/proto/grpc/testing/xds/v3:http_connection_manager_proto",
        "//src/proto/grpc/testing/xds/v3:http_filter_rbac_proto",
        "//src/proto/grpc/testing/xds/v3:listener_proto",
        "//src/proto/grpc/testing/xds/v3:route_proto",
        "//src/proto/grpc/testing/xds/v3:router_proto",
        "//src/proto/grpc/testing/xds/v3:tls_proto",
        "//test/core/util:grpc_test_util",
        "//test/cpp/util:test_config",
        "//test/cpp/util:test_util",
        "//test/cpp/util:tls_test_utils",
    ],
)

grpc_cc_test(
    name = "xds_cluster_end2end_test",
    size = "large",
    srcs = ["xds_cluster_end2end_test.cc"],
    external_deps = [
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
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//test/core/util:grpc_test_util",
        "//test/cpp/end2end:connection_attempt_injector",
    ],
)

grpc_cc_test(
    name = "xds_cluster_type_end2end_test",
    size = "large",
    srcs = ["xds_cluster_type_end2end_test.cc"],
    external_deps = [
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
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//:grpc_resolver_fake",
        "//src/proto/grpc/testing/xds/v3:aggregate_cluster_proto",
        "//test/core/util:grpc_test_util",
        "//test/cpp/end2end:connection_attempt_injector",
    ],
)

grpc_cc_test(
    name = "xds_core_end2end_test",
    size = "large",
    srcs = ["xds_core_end2end_test.cc"],
    external_deps = [
        "gtest",
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
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//test/core/util:grpc_test_util",
        "//test/core/util:scoped_env_var",
    ],
)

grpc_cc_test(
    name = "xds_csds_end2end_test",
    size = "large",
    srcs = ["xds_csds_end2end_test.cc"],
    external_deps = [
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
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//:grpcpp_csds",
        "//src/proto/grpc/testing/xds/v3:cluster_proto",
        "//src/proto/grpc/testing/xds/v3:endpoint_proto",
        "//src/proto/grpc/testing/xds/v3:listener_proto",
        "//src/proto/grpc/testing/xds/v3:route_proto",
        "//test/core/util:grpc_test_util",
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
        "no_test_ios",
        "no_windows",
        "xds_end2end_test",
    ],  # TODO(jtattermusch): fix test on windows
    deps = [
        ":xds_end2end_test_lib",
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//src/proto/grpc/testing/xds/v3:cluster_proto",
        "//src/proto/grpc/testing/xds/v3:fault_proto",
        "//src/proto/grpc/testing/xds/v3:http_connection_manager_proto",
        "//src/proto/grpc/testing/xds/v3:router_proto",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "xds_outlier_detection_end2end_test",
    size = "large",
    srcs = ["xds_outlier_detection_end2end_test.cc"],
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
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//src/proto/grpc/testing/xds/v3:fault_proto",
        "//src/proto/grpc/testing/xds/v3:router_proto",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "xds_wrr_end2end_test",
    size = "large",
    srcs = ["xds_wrr_end2end_test.cc"],
    external_deps = [
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
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//src/proto/grpc/testing/xds/v3:client_side_weighted_round_robin_proto",
        "//src/proto/grpc/testing/xds/v3:cluster_proto",
        "//src/proto/grpc/testing/xds/v3:wrr_locality_proto",
        "//test/core/util:grpc_test_util",
        "//test/core/util:scoped_env_var",
    ],
)

grpc_cc_test(
    name = "xds_ring_hash_end2end_test",
    size = "large",
    srcs = ["xds_ring_hash_end2end_test.cc"],
    external_deps = [
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
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//:grpc_resolver_fake",
        "//src/proto/grpc/testing/xds/v3:aggregate_cluster_proto",
        "//src/proto/grpc/testing/xds/v3:cluster_proto",
        "//src/proto/grpc/testing/xds/v3:endpoint_proto",
        "//src/proto/grpc/testing/xds/v3:listener_proto",
        "//src/proto/grpc/testing/xds/v3:route_proto",
        "//test/core/util:grpc_test_util",
        "//test/cpp/end2end:connection_attempt_injector",
    ],
)

grpc_cc_test(
    name = "xds_rls_end2end_test",
    size = "large",
    srcs = ["xds_rls_end2end_test.cc"],
    external_deps = [
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
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//src/proto/grpc/lookup/v1:rls_config_proto",
        "//src/proto/grpc/lookup/v1:rls_proto",
        "//test/core/util:grpc_test_util",
        "//test/core/util:scoped_env_var",
        "//test/cpp/end2end:rls_server",
    ],
)

grpc_cc_test(
    name = "xds_routing_end2end_test",
    size = "large",
    srcs = ["xds_routing_end2end_test.cc"],
    external_deps = [
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
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//src/proto/grpc/testing/xds/v3:fault_proto",
        "//src/proto/grpc/testing/xds/v3:router_proto",
        "//test/core/util:grpc_test_util",
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
        "//src/proto/grpc/testing:echo_messages_proto",
        "//src/proto/grpc/testing:echo_proto",
        "//test/core/util:grpc_test_util",
        "//test/cpp/end2end:test_service_impl",
        "//test/cpp/util:test_util",
    ],
)

grpc_cc_test(
    name = "xds_override_host_end2end_test",
    size = "large",
    srcs = ["xds_override_host_end2end_test.cc"],
    external_deps = [
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
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//src/proto/grpc/testing/xds/v3:router_proto",
        "//src/proto/grpc/testing/xds/v3:stateful_session_cookie_proto",
        "//src/proto/grpc/testing/xds/v3:stateful_session_proto",
        "//test/core/util:grpc_test_util",
        "//test/core/util:scoped_env_var",
    ],
)
