# Copyright 2016 gRPC authors.
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
load(":generate_tests.bzl", "grpc_end2end_tests")

licenses(["notice"])

grpc_package(name = "test/core/end2end")

grpc_cc_library(
    name = "cq_verifier",
    srcs = ["cq_verifier.cc"],
    hdrs = ["cq_verifier.h"],
    external_deps = [
        "absl/strings",
        "absl/strings:str_format",
        "absl/types:variant",
    ],
    language = "C++",
    visibility = ["//test:__subpackages__"],
    deps = [
        "//:debug_location",
        "//:gpr",
        "//:grpc",
        "//src/core:match",
        "//src/core:time",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_library(
    name = "ssl_test_data",
    srcs = [
        "data/client_certs.cc",
        "data/server1_cert.cc",
        "data/server1_key.cc",
        "data/test_root_cert.cc",
    ],
    hdrs = ["data/ssl_test_data.h"],
    language = "C++",
    visibility = ["//test:__subpackages__"],
)

grpc_cc_library(
    name = "http_proxy",
    srcs = ["fixtures/http_proxy_fixture.cc"],
    hdrs = ["fixtures/http_proxy_fixture.h"],
    external_deps = [
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
    ],
    language = "C++",
    deps = [
        "//:config",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc",
        "//:httpcli",
        "//:sockaddr_utils",
        "//src/core:channel_args",
        "//src/core:channel_args_endpoint_config",
        "//src/core:channel_args_preconditioning",
        "//src/core:closure",
        "//src/core:error",
        "//src/core:iomgr_fwd",
        "//src/core:pollset_set",
        "//src/core:resolved_address",
        "//src/core:slice_refcount",
        "//src/core:status_helper",
        "//src/core:time",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_library(
    name = "proxy",
    srcs = ["fixtures/proxy.cc"],
    hdrs = ["fixtures/proxy.h"],
    language = "C++",
    deps = [
        "//:gpr",
        "//:grpc",
        "//:grpc_public_hdrs",
        "//src/core:channel_args",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_library(
    name = "local_util",
    srcs = ["fixtures/local_util.cc"],
    hdrs = [
        "end2end_tests.h",
        "fixtures/local_util.h",
    ],
    language = "C++",
    deps = [
        "//:gpr",
        "//:grpc_public_hdrs",
    ],
)

grpc_cc_test(
    name = "bad_server_response_test",
    srcs = ["bad_server_response_test.cc"],
    language = "C++",
    deps = [
        "cq_verifier",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc",
        "//:grpc_public_hdrs",
        "//src/core:closure",
        "//src/core:error",
        "//src/core:gpr_atm",
        "//src/core:iomgr_fwd",
        "//src/core:slice",
        "//src/core:status_helper",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "connection_refused_test",
    srcs = ["connection_refused_test.cc"],
    language = "C++",
    deps = [
        "cq_verifier",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc_public_hdrs",
        "//src/core:channel_args",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "dualstack_socket_test",
    srcs = ["dualstack_socket_test.cc"],
    external_deps = [
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
    ],
    language = "C++",
    tags = ["no_windows"],
    deps = [
        "cq_verifier",
        "//:gpr",
        "//:grpc",
        "//:grpc_public_hdrs",
        "//:sockaddr_utils",
        "//src/core:error",
        "//src/core:iomgr_port",
        "//src/core:resolved_address",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "goaway_server_test",
    srcs = ["goaway_server_test.cc"],
    external_deps = [
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
    ],
    language = "C++",
    deps = [
        "cq_verifier",
        "//:debug_location",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc",
        "//:grpc_public_hdrs",
        "//:grpc_resolver_dns_ares",
        "//:server_address",
        "//src/core:channel_args",
        "//src/core:closure",
        "//src/core:default_event_engine",
        "//src/core:error",
        "//src/core:grpc_sockaddr",
        "//src/core:iomgr_fwd",
        "//src/core:resolved_address",
        "//src/core:time",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "inproc_callback_test",
    srcs = ["inproc_callback_test.cc"],
    external_deps = ["absl/strings:str_format"],
    language = "C++",
    uses_event_engine = False,
    uses_polling = False,
    deps = [
        "local_util",
        "//:gpr",
        "//:grpc_public_hdrs",
        "//src/core:grpc_transport_inproc",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "invalid_call_argument_test",
    srcs = ["invalid_call_argument_test.cc"],
    language = "C++",
    deps = [
        "cq_verifier",
        "//:gpr",
        "//:gpr_platform",
        "//:grpc_public_hdrs",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "multiple_server_queues_test",
    srcs = ["multiple_server_queues_test.cc"],
    language = "C++",
    deps = [
        "//:event_engine_base_hdrs",
        "//:gpr",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "no_server_test",
    srcs = ["no_server_test.cc"],
    language = "C++",
    deps = [
        "cq_verifier",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc_public_hdrs",
        "//:grpc_resolver_fake",
        "//:ref_counted_ptr",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_end2end_tests()

grpc_cc_test(
    name = "h2_ssl_cert_test",
    srcs = [
        "end2end_tests.h",
        "h2_ssl_cert_test.cc",
    ],
    external_deps = [
        "gtest",
        "libcrypto",
    ],
    language = "C++",
    deps = [
        "cq_verifier",
        "local_util",
        "ssl_test_data",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc",
        "//:grpc_public_hdrs",
        "//:grpc_security_base",
        "//:tsi_ssl_credentials",
        "//src/core:channel_args",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "h2_ssl_session_reuse_test",
    srcs = ["h2_ssl_session_reuse_test.cc"],
    data = [
        "//src/core/tsi/test_creds:ca.pem",
        "//src/core/tsi/test_creds:client.key",
        "//src/core/tsi/test_creds:client.pem",
        "//src/core/tsi/test_creds:server1.key",
        "//src/core/tsi/test_creds:server1.pem",
    ],
    external_deps = ["gtest"],
    language = "C++",
    deps = [
        "cq_verifier",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc",
        "//:grpc_public_hdrs",
        "//:tsi_ssl_credentials",
        "//src/core:channel_args",
        "//src/core:error",
        "//src/core:useful",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "h2_tls_peer_property_external_verifier_test",
    srcs = ["h2_tls_peer_property_external_verifier_test.cc"],
    data = [
        "//src/core/tsi/test_creds:ca.pem",
        "//src/core/tsi/test_creds:client.key",
        "//src/core/tsi/test_creds:client.pem",
        "//src/core/tsi/test_creds:server1.key",
        "//src/core/tsi/test_creds:server1.pem",
    ],
    external_deps = ["gtest"],
    language = "C++",
    deps = [
        "cq_verifier",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc",
        "//:grpc_public_hdrs",
        "//:tsi_ssl_credentials",
        "//src/core:channel_args",
        "//src/core:error",
        "//src/core:useful",
        "//test/core/util:grpc_test_util",
    ],
)
