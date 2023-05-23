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
load("//test/core/util:grpc_fuzzer.bzl", "grpc_proto_fuzzer")

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
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "certificate_provider_store_test",
    srcs = ["certificate_provider_store_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:grpc_test_util",
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
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "xds_channel_stack_modifier_test",
    srcs = ["xds_channel_stack_modifier_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    uses_event_engine = False,
    uses_polling = False,
    tags = ["no_test_ios"],
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc_base",
        "//:grpc_opencensus_plugin",
        "//src/core:channel_args",
        "//src/core:grpc_xds_channel_stack_modifier",
        "//test/core/util:grpc_test_util",
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
        "//test/core/util:grpc_test_util",
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
        "//src/proto/grpc/testing/xds/v3:client_side_weighted_round_robin_proto",
        "//src/proto/grpc/testing/xds/v3:cluster_proto",
        "//src/proto/grpc/testing/xds/v3:ring_hash_proto",
        "//src/proto/grpc/testing/xds/v3:round_robin_proto",
        "//src/proto/grpc/testing/xds/v3:typed_struct_proto",
        "//src/proto/grpc/testing/xds/v3:udpa_typed_struct_proto",
        "//src/proto/grpc/testing/xds/v3:wrr_locality_proto",
        "//test/core/util:grpc_test_util",
        "//test/cpp/util:grpc_cli_utils",
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
        "//src/proto/grpc/testing/xds/v3:audit_logger_stream_proto",
        "//src/proto/grpc/testing/xds/v3:rbac_proto",
        "//src/proto/grpc/testing/xds/v3:typed_struct_proto",
        "//test/core/util:grpc_test_util",
        "//test/cpp/util:grpc_cli_utils",
    ],
)

grpc_cc_library(
    name = "xds_transport_fake",
    testonly = True,
    srcs = ["xds_transport_fake.cc"],
    hdrs = ["xds_transport_fake.h"],
    external_deps = [
        "absl/strings",
        "absl/types:optional",
    ],
    language = "C++",
    deps = [
        "//:orphanable",
        "//:ref_counted_ptr",
        "//:xds_client",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "xds_client_test",
    srcs = ["xds_client_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    shard_count = 10,
    uses_event_engine = True,
    uses_polling = False,
    deps = [
        ":xds_transport_fake",
        "//:xds_client",
        "//src/proto/grpc/testing/xds/v3:discovery_proto",
        "//test/core/util:grpc_test_util",
        "//test/core/util:scoped_env_var",
    ],
)

grpc_proto_fuzzer(
    name = "xds_client_fuzzer",
    srcs = ["xds_client_fuzzer.cc"],
    corpus = "xds_client_corpora",
    language = "C++",
    proto = "xds_client_fuzzer.proto",
    proto_deps = [
        "//src/proto/grpc/testing/xds/v3:discovery_proto",
    ],
    tags = ["no_windows"],
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        ":xds_transport_fake",
        "//src/core:grpc_xds_client",
        "//test/core/util:grpc_test_util",
        # These proto deps are needed to ensure that we can read these
        # resource types out of the google.protobuf.Any fields in the
        # textproto files in the corpora.
        "//src/proto/grpc/testing/xds/v3:listener_proto",
        "//src/proto/grpc/testing/xds/v3:route_proto",
        "//src/proto/grpc/testing/xds/v3:cluster_proto",
        "//src/proto/grpc/testing/xds/v3:endpoint_proto",
        "//src/proto/grpc/testing/xds/v3:http_connection_manager_proto",
        "//src/proto/grpc/testing/xds/v3:router_proto",
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
        "//src/proto/grpc/testing/xds/v3:tls_proto",
        "//src/proto/grpc/testing/xds/v3:typed_struct_proto",
        "//src/proto/grpc/testing/xds/v3:udpa_typed_struct_proto",
        "//test/core/util:grpc_test_util",
        "//test/cpp/util:grpc_cli_utils",
    ],
)

grpc_cc_test(
    name = "xds_http_filters_test",
    srcs = ["xds_http_filters_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "//:gpr",
        "//:grpc",
        "//src/proto/grpc/testing/xds/v3:fault_proto",
        "//src/proto/grpc/testing/xds/v3:http_filter_rbac_proto",
        "//src/proto/grpc/testing/xds/v3:router_proto",
        "//src/proto/grpc/testing/xds/v3:stateful_session_cookie_proto",
        "//src/proto/grpc/testing/xds/v3:stateful_session_proto",
        "//src/proto/grpc/testing/xds/v3:typed_struct_proto",
        "//test/core/util:grpc_test_util",
        "//test/core/util:scoped_env_var",
        "//test/cpp/util:grpc_cli_utils",
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
        "//src/core:grpc_xds_client",
        "//src/proto/grpc/testing/xds/v3:fault_proto",
        "//src/proto/grpc/testing/xds/v3:http_connection_manager_proto",
        "//src/proto/grpc/testing/xds/v3:http_filter_rbac_proto",
        "//src/proto/grpc/testing/xds/v3:listener_proto",
        "//src/proto/grpc/testing/xds/v3:router_proto",
        "//src/proto/grpc/testing/xds/v3:tls_proto",
        "//src/proto/grpc/testing/xds/v3:typed_struct_proto",
        "//test/core/util:grpc_test_util",
        "//test/cpp/util:grpc_cli_utils",
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
        "//src/core:grpc_xds_client",
        "//src/proto/grpc/lookup/v1:rls_config_proto",
        "//src/proto/grpc/testing/xds/v3:fault_proto",
        "//src/proto/grpc/testing/xds/v3:http_filter_rbac_proto",
        "//src/proto/grpc/testing/xds/v3:route_proto",
        "//src/proto/grpc/testing/xds/v3:typed_struct_proto",
        "//test/core/util:grpc_test_util",
        "//test/core/util:scoped_env_var",
        "//test/cpp/util:grpc_cli_utils",
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
        "//src/core:grpc_xds_client",
        "//src/proto/grpc/testing/xds/v3:aggregate_cluster_proto",
        "//src/proto/grpc/testing/xds/v3:cluster_proto",
        "//src/proto/grpc/testing/xds/v3:round_robin_proto",
        "//src/proto/grpc/testing/xds/v3:tls_proto",
        "//src/proto/grpc/testing/xds/v3:typed_struct_proto",
        "//src/proto/grpc/testing/xds/v3:wrr_locality_proto",
        "//test/core/util:grpc_test_util",
        "//test/core/util:scoped_env_var",
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
        "//src/core:channel_args",
        "//src/core:grpc_xds_client",
        "//src/proto/grpc/testing/xds/v3:endpoint_proto",
        "//test/core/util:grpc_test_util",
        "//test/core/util:scoped_env_var",
    ],
)
