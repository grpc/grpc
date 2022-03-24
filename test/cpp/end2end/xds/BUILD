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
        "//src/proto/grpc/testing/xds:ads_for_test_proto",
        "//src/proto/grpc/testing/xds:lrs_for_test_proto",
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
    ],  # TODO(jtattermusch): fix test on windows
    deps = [
        ":xds_server",
        "//:gpr",
        "//:grpc",
        "//:grpc++",
        "//:grpc_resolver_fake",
        "//:grpcpp_csds",
        "//src/proto/grpc/lookup/v1:rls_config_proto",
        "//src/proto/grpc/lookup/v1:rls_proto",
        "//src/proto/grpc/testing:echo_messages_proto",
        "//src/proto/grpc/testing:echo_proto",
        "//src/proto/grpc/testing/duplicate:echo_duplicate_proto",
        "@envoy_api//envoy/api/v2:pkg_cc_grpc",
        "@envoy_api//envoy/config/cluster/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/endpoint/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/listener/v3:pkg_cc_proto",
        "@envoy_api//envoy/config/route/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/clusters/aggregate/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/common/fault/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/fault/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/http/router/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/filters/network/http_connection_manager/v3:pkg_cc_proto",
        "@envoy_api//envoy/extensions/transport_sockets/tls/v3:pkg_cc_proto",
        "@envoy_api//envoy/service/discovery/v2:pkg_cc_grpc",
        "@envoy_api//envoy/service/discovery/v3:pkg_cc_grpc",
        "@envoy_api//envoy/service/load_stats/v2:pkg_cc_grpc",
        "@envoy_api//envoy/service/load_stats/v3:pkg_cc_grpc",
        "@envoy_api//envoy/service/status/v3:pkg_cc_grpc",
        "//test/core/util:grpc_test_util",
        "//test/cpp/end2end:counted_service",
        "//test/cpp/end2end:rls_server",
        "//test/cpp/end2end:test_service_impl",
        "//test/cpp/util:test_config",
        "//test/cpp/util:test_util",
        "//test/cpp/util:tls_test_utils",
    ],
)

grpc_cc_test(
    name = "xds_credentials_end2end_test",
    srcs = ["xds_credentials_end2end_test.cc"],
    external_deps = [
        "gtest",
    ],
    tags = ["no_test_ios"],
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
