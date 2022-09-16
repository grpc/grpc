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

grpc_package(name = "test/core/xds")

licenses(["notice"])

grpc_cc_test(
    name = "xds_bootstrap_test",
    srcs = ["xds_bootstrap_test.cc"],
    external_deps = [
        "gtest",
    ],
    language = "C++",
    deps = [
        "//:gpr",
        "//:grpc_xds_client",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "certificate_provider_store_test",
    srcs = ["certificate_provider_store_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
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
    deps = [
        "//:gpr",
        "//:grpc",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "google_mesh_ca_certificate_provider_factory_test",
    srcs = ["google_mesh_ca_certificate_provider_factory_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc_google_mesh_ca_certificate_provider_factory",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "xds_channel_stack_modifier_test",
    srcs = ["xds_channel_stack_modifier_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
    tags = ["no_test_ios"],
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc_base",
        "//:grpc_opencensus_plugin",
        "//:grpc_xds_channel_stack_modifier",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "xds_certificate_provider_test",
    srcs = ["xds_certificate_provider_test.cc"],
    external_deps = ["gtest"],
    language = "C++",
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
    uses_polling = False,
    deps = [
        "//:gpr",
        "//:grpc",
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
    uses_event_engine = True,
    uses_polling = False,
    deps = [
        ":xds_transport_fake",
        "//:xds_client",
        "//src/proto/grpc/testing/xds/v3:discovery_proto",
        "//test/core/util:grpc_test_util",
    ],
)
