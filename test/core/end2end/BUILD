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

load("//bazel:grpc_build_system.bzl", "grpc_cc_library", "grpc_cc_proto_library", "grpc_cc_test", "grpc_internal_proto_library", "grpc_package")
load("//test/core/end2end:grpc_core_end2end_test.bzl", "grpc_core_end2end_test_suite")

licenses(["notice"])

grpc_package(name = "test/core/end2end")

# grpc_core_end2end_test_suite() currently needs to consume the source for tests directly.
exports_files(
    glob(["tests/**"]),
    visibility = [
        "//:__subpackages__",
        "//bazel:core_end2end_tests",
    ],
)

grpc_cc_library(
    name = "cq_verifier",
    testonly = 1,
    srcs = ["cq_verifier.cc"],
    hdrs = ["cq_verifier.h"],
    external_deps = [
        "absl/container:flat_hash_map",
        "absl/functional:any_invocable",
        "absl/log:check",
        "absl/log:log",
        "absl/strings",
        "absl/strings:str_format",
        "gtest",
    ],
    visibility = ["//:__subpackages__"],
    deps = [
        "//:debug_location",
        "//:gpr",
        "//:grpc",
        "//src/core:match",
        "//src/core:time",
        "//test/core/test_util:grpc_test_util",
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
    visibility = ["//:__subpackages__"],
)

grpc_cc_library(
    name = "http_proxy",
    srcs = ["fixtures/http_proxy_fixture.cc"],
    hdrs = ["fixtures/http_proxy_fixture.h"],
    external_deps = [
        "absl/log:check",
        "absl/log:log",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
    ],
    visibility = ["//test/cpp/end2end/xds:__subpackages__"],
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
        "//src/core:default_event_engine",
        "//src/core:error",
        "//src/core:iomgr_fwd",
        "//src/core:pollset_set",
        "//src/core:resolved_address",
        "//src/core:slice_refcount",
        "//src/core:status_helper",
        "//src/core:time",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_library(
    name = "proxy",
    srcs = ["fixtures/proxy.cc"],
    hdrs = ["fixtures/proxy.h"],
    external_deps = [
        "absl/log:check",
        "absl/log:log",
    ],
    deps = [
        "//:channel_arg_names",
        "//:gpr",
        "//:grpc",
        "//:grpc_public_hdrs",
        "//src/core:channel_args",
        "//test/core/test_util:grpc_test_util",
    ],
)

# Notes:
# - We have several platforms on which fuzzers do not work,
#   but we want to fuzz on Linux to find all the nice bugs that it finds.
# - We also don't want to keep our own test running infrastructure
#   to make all this work.
# - We have an expectation that fuzztest will work as far as running
#   tests (maybe without fuzzing) soon.
# - We can't customize the defines that cmake pulls in
# - fuzztest isn't yet integrated with our cmake infrastructure
# So:
# - For now, define a library for fuzz testing, and one for gtest
#   (and one for both to keep ensuring that works)
# - Build tests on all platforms, exclude fuzzers from platforms
#   where we don't work
# - Choose defines carefully so that the defaults are what we want
#   on cmake (basically, gtest - and no fuzztest)
[
    grpc_cc_library(
        name = "end2end_test_lib" +
               ("_fuzztest" if fuzz else "_no_fuzztest") +
               ("_gtest" if gtest else "_no_gtest"),
        testonly = 1,
        srcs = [
            "end2end_test_suites.cc",
            "end2end_tests.cc",
            "fixtures/local_util.cc",
        ],
        hdrs = [
            "end2end_tests.h",
            "fixtures/h2_oauth2_common.h",
            "fixtures/h2_ssl_cred_reload_fixture.h",
            "fixtures/h2_ssl_tls_common.h",
            "fixtures/h2_tls_common.h",
            "fixtures/inproc_fixture.h",
            "fixtures/local_util.h",
            "fixtures/secure_fixture.h",
            "fixtures/sockpair_fixture.h",
            "tests/cancel_test_helpers.h",
        ],
        defines =
            (["GRPC_END2END_TEST_INCLUDE_FUZZER"] if fuzz else []) +
            ([] if gtest else ["GRPC_END2END_TEST_NO_GTEST"]),
        external_deps = [
            "absl/functional:any_invocable",
            "absl/log:check",
            "absl/log:log",
            "absl/memory",
            "absl/meta:type_traits",
            "absl/random",
            "absl/strings",
            "gtest",
        ] + (["fuzztest"] if fuzz else []),
        tags = ["no_windows"] if fuzz else [],
        deps = [
            "cq_verifier",
            "end2end_test_fuzzer_cc_proto",
            "http_proxy",
            "proxy",
            "//:config",
            "//:debug_location",
            "//:gpr",
            "//:grpc",
            "//:grpc_public_hdrs",
            "//src/core:bitset",
            "//src/core:channel_args",
            "//src/core:chaotic_good_connector",
            "//src/core:chaotic_good_server",
            "//src/core:default_event_engine",
            "//src/core:no_destruct",
            "//src/core:slice",
            "//src/core:time",
            "//test/core/call:batch_builder",
            "//test/core/event_engine:event_engine_test_utils",
            "//test/core/event_engine/fuzzing_event_engine",
            "//test/core/test_util:fuzz_config_vars",
            "//test/core/test_util:grpc_test_util",
            "//test/core/test_util:test_call_creds",
        ] + (["//test/core/test_util:fuzz_config_vars_helpers"] if fuzz else []),
        alwayslink = 1,
    )
    for (fuzz, gtest) in [
        (True, False),
        (False, True),
        (True, True),
    ]
]

grpc_cc_test(
    name = "bad_server_response_test",
    srcs = ["bad_server_response_test.cc"],
    external_deps = [
        "absl/log:check",
        "absl/log:log",
    ],
    tags = [
        "grpc:broken-internally",  # remove once converted to gtest
        "grpc:no-internal-poller",
        "requires-net:ipv4",
        "requires-net:loopback",
    ],
    deps = [
        "cq_verifier",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc",
        "//:grpc_public_hdrs",
        "//src/core:closure",
        "//src/core:error",
        "//src/core:event_engine_shim",
        "//src/core:gpr_atm",
        "//src/core:iomgr_fwd",
        "//src/core:notification",
        "//src/core:slice",
        "//src/core:status_helper",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "connection_refused_test",
    srcs = ["connection_refused_test.cc"],
    external_deps = [
        "absl/log:check",
        "absl/log:log",
    ],
    tags = [
        "grpc:broken-internally",  # remove once converted to gtest
    ],
    deps = [
        "cq_verifier",
        "//:channel_arg_names",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc_public_hdrs",
        "//src/core:channel_args",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "dualstack_socket_test",
    srcs = ["dualstack_socket_test.cc"],
    external_deps = [
        "absl/log:check",
        "absl/log:log",
        "absl/status:statusor",
        "absl/strings",
        "absl/strings:str_format",
    ],
    tags = [
        "grpc:broken-internally",  # remove once converted to gtest
        "grpc:no-internal-poller",
        "no_windows",
        "requires-net:ipv4",
        "requires-net:loopback",
    ],
    deps = [
        "cq_verifier",
        "//:gpr",
        "//:grpc",
        "//:grpc_public_hdrs",
        "//src/core:error",
        "//src/core:event_engine_common",
        "//src/core:event_engine_tcp_socket_utils",
        "//src/core:iomgr_port",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "goaway_server_test",
    srcs = ["goaway_server_test.cc"],
    external_deps = [
        "absl/log:check",
        "absl/log:log",
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
    ],
    tags = [
        "requires-net:ipv4",
        "requires-net:loopback",
        # relies on ares for dns resolution
        "grpc:broken-internally",
    ],
    deps = [
        "cq_verifier",
        "//:channel_arg_names",
        "//:debug_location",
        "//:endpoint_addresses",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc",
        "//:grpc_public_hdrs",
        "//:grpc_resolver_dns_ares",
        "//src/core:channel_args",
        "//src/core:closure",
        "//src/core:default_event_engine",
        "//src/core:error",
        "//src/core:experiments",
        "//src/core:grpc_sockaddr",
        "//src/core:iomgr_fwd",
        "//src/core:resolved_address",
        "//src/core:time",
        "//test/core/event_engine:delegating_event_engine",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "invalid_call_argument_test",
    srcs = ["invalid_call_argument_test.cc"],
    external_deps = [
        "absl/log:check",
        "absl/log:log",
    ],
    tags = [
        "grpc:broken-internally",  # remove once converted to gtest
    ],
    deps = [
        "cq_verifier",
        "//:gpr",
        "//:gpr_platform",
        "//:grpc_public_hdrs",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "multiple_server_queues_test",
    srcs = ["multiple_server_queues_test.cc"],
    deps = [
        "//:event_engine_base_hdrs",
        "//:gpr",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "no_server_test",
    srcs = ["no_server_test.cc"],
    external_deps = [
        "absl/log:check",
        "absl/log:log",
        "absl/status",
        "absl/status:statusor",
        "absl/time",
    ],
    deps = [
        "cq_verifier",
        "//:endpoint_addresses",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc_public_hdrs",
        "//:grpc_resolver",
        "//:grpc_resolver_fake",
        "//:ref_counted_ptr",
        "//src/core:channel_args",
        "//src/core:grpc_service_config",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_cc_test(
    name = "h2_ssl_cert_test",
    srcs = [
        "end2end_tests.h",
        "h2_ssl_cert_test.cc",
    ],
    external_deps = [
        "absl/functional:any_invocable",
        "absl/log:check",
        "absl/log:log",
        "absl/memory",
        "absl/meta:type_traits",
        "absl/strings",
        "gtest",
    ],
    shard_count = 10,
    deps = [
        "cq_verifier",
        "end2end_test_lib_no_fuzztest_gtest",
        "ssl_test_data",
        "//:channel_arg_names",
        "//:config_vars",
        "//:debug_location",
        "//:gpr",
        "//:grpc",
        "//:grpc_public_hdrs",
        "//:grpc_security_base",
        "//src/core:bitset",
        "//src/core:channel_args",
        "//src/core:slice",
        "//src/core:time",
        "//test/core/event_engine:event_engine_test_utils",
        "//test/core/test_util:grpc_test_util",
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
    external_deps = [
        "absl/log:check",
        "gtest",
    ],
    deps = [
        "cq_verifier",
        "//:channel_arg_names",
        "//:config_vars",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc",
        "//:grpc_public_hdrs",
        "//src/core:channel_args",
        "//src/core:error",
        "//src/core:useful",
        "//test/core/test_util:grpc_test_util",
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
    external_deps = [
        "absl/log:check",
        "gtest",
    ],
    deps = [
        "cq_verifier",
        "//:channel_arg_names",
        "//:config_vars",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc",
        "//:grpc_public_hdrs",
        "//src/core:channel_args",
        "//src/core:error",
        "//src/core:useful",
        "//test/core/test_util:grpc_test_util",
    ],
)

grpc_internal_proto_library(
    name = "end2end_test_fuzzer_proto",
    srcs = ["end2end_test_fuzzer.proto"],
    deps = [
        "//test/core/event_engine/fuzzing_event_engine:fuzzing_event_engine_proto",
        "//test/core/test_util:fuzz_config_vars_proto",
    ],
)

grpc_cc_proto_library(
    name = "end2end_test_fuzzer_cc_proto",
    deps = ["end2end_test_fuzzer_proto"],
)

grpc_core_end2end_test_suite(
    name = "end2end_http2",
    config_src = "end2end_http2_config.cc",
    flaky = True,
    tags = [
        "grpc:no-internal-poller",
    ],
)

grpc_core_end2end_test_suite(
    name = "end2end_http2_security",
    config_src = "end2end_http2_security_config.cc",
    flaky = True,
    tags = [
        "grpc:no-internal-poller",
    ],
)

grpc_core_end2end_test_suite(
    name = "end2end_posix",
    config_src = "end2end_posix_config.cc",
    flaky = True,
    tags = [
        "grpc:no-internal-poller",
    ],
)

grpc_core_end2end_test_suite(
    name = "end2end_chaotic_good",
    config_src = "end2end_chaotic_good_config.cc",
    flaky = True,
    tags = [
        "grpc:no-internal-poller",
        "no_windows",
    ],
)

grpc_core_end2end_test_suite(
    name = "end2end_inproc",
    config_src = "end2end_inproc_config.cc",
    flaky = True,
    tags = [
        "grpc:no-internal-poller",
    ],
)
