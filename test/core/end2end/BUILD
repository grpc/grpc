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

load("//bazel:grpc_build_system.bzl", "grpc_cc_library", "grpc_cc_test", "grpc_package", "grpc_proto_library")
load("grpc_core_end2end_test.bzl", "grpc_core_end2end_test")

licenses(["notice"])

grpc_package(name = "test/core/end2end")

grpc_cc_library(
    name = "cq_verifier",
    testonly = 1,
    srcs = ["cq_verifier.cc"],
    hdrs = ["cq_verifier.h"],
    external_deps = [
        "absl/container:flat_hash_map",
        "absl/functional:any_invocable",
        "absl/strings",
        "absl/strings:str_format",
        "absl/types:variant",
        "gtest",
    ],
    language = "C++",
    visibility = ["//:__subpackages__"],
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
    visibility = ["//:__subpackages__"],
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
        "//:channel_arg_names",
        "//:gpr",
        "//:grpc",
        "//:grpc_public_hdrs",
        "//src/core:channel_args",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_library(
    name = "end2end_test_lib",
    testonly = 1,
    srcs = [
        "end2end_tests.cc",
    ],
    hdrs = [
        "end2end_tests.h",
    ],
    external_deps = [
        "absl/functional:any_invocable",
        "absl/memory",
        "absl/meta:type_traits",
        "absl/random",
        "absl/strings",
        "absl/types:optional",
        "absl/types:variant",
        "gtest",
    ],
    deps = [
        "cq_verifier",
        "//:config",
        "//:debug_location",
        "//:gpr",
        "//:grpc",
        "//:grpc_public_hdrs",
        "//src/core:bitset",
        "//src/core:channel_args",
        "//src/core:default_event_engine",
        "//src/core:no_destruct",
        "//src/core:slice",
        "//src/core:time",
        "//test/core/event_engine:event_engine_test_utils",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_library(
    name = "fixture_support",
    testonly = 1,
    srcs = ["fixtures/local_util.cc"],
    hdrs = [
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
    external_deps = [
        "absl/status",
        "absl/status:statusor",
        "absl/strings",
        "gtest",
    ],
    language = "C++",
    deps = [
        "end2end_test_lib",
        "//:channel_arg_names",
        "//:config",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc",
        "//:grpc_public_hdrs",
        "//:grpc_security_base",
        "//:grpc_transport_chttp2",
        "//:ref_counted_ptr",
        "//src/core:channel_args",
        "//src/core:channel_args_preconditioning",
        "//src/core:channel_stack_type",
        "//src/core:error",
        "//src/core:grpc_ssl_credentials",
        "//src/core:grpc_tls_credentials",
        "//src/core:grpc_transport_inproc",
        "//src/core:slice",
        "//src/core:transport_fwd",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_cc_library(
    name = "end2end_test_suites",
    testonly = 1,
    srcs = [
        "end2end_test_suites.cc",
    ],
    external_deps = [
        "absl/base:core_headers",
        "absl/random",
        "absl/status",
        "absl/strings:str_format",
        "absl/types:optional",
        "gtest",
    ],
    deps = [
        "end2end_test_lib",
        "fixture_support",
        "http_proxy",
        "proxy",
        "//:channel_arg_names",
        "//:exec_ctx",
        "//:gpr",
        "//:grpc",
        "//:grpc_public_hdrs",
        "//:grpc_trace",
        "//src/core:channel_args",
        "//src/core:error",
        "//src/core:grpc_fake_credentials",
        "//src/core:iomgr_port",
        "//src/core:no_destruct",
        "//test/core/util:grpc_test_util",
    ],
    alwayslink = 1,
)

grpc_cc_library(
    name = "end2end_test_main",
    testonly = 1,
    srcs = [
        "end2end_test_main.cc",
    ],
    external_deps = [
        "absl/functional:any_invocable",
        "absl/strings",
        "absl/types:optional",
        "gtest",
    ],
    tags = ["nofixdeps"],
    deps = [
        "end2end_test_lib",
        "end2end_test_suites",
        "fixture_support",
        "//:config_vars",
        "//test/core/util:grpc_test_util",
    ],
)

grpc_core_end2end_test(name = "bad_ping")

grpc_core_end2end_test(name = "binary_metadata")

grpc_core_end2end_test(name = "call_creds")

grpc_core_end2end_test(name = "call_host_override")

grpc_core_end2end_test(
    name = "cancel_after_accept",
    shard_count = 50,
)

grpc_core_end2end_test(
    name = "cancel_after_client_done",
    shard_count = 50,
)

grpc_core_end2end_test(
    name = "cancel_after_invoke",
    shard_count = 50,
)

grpc_core_end2end_test(
    name = "cancel_after_round_trip",
    shard_count = 50,
)

grpc_core_end2end_test(name = "cancel_before_invoke")

grpc_core_end2end_test(name = "cancel_in_a_vacuum")

grpc_core_end2end_test(name = "cancel_with_status")

grpc_core_end2end_test(name = "channelz")

grpc_core_end2end_test(name = "client_streaming")

grpc_core_end2end_test(name = "compressed_payload")

grpc_core_end2end_test(name = "connectivity")

grpc_core_end2end_test(name = "default_host")

grpc_core_end2end_test(name = "disappearing_server")

grpc_core_end2end_test(name = "empty_batch")

grpc_core_end2end_test(name = "filter_causes_close")

grpc_core_end2end_test(name = "filter_context")

grpc_core_end2end_test(name = "filter_init_fails")

grpc_core_end2end_test(name = "filtered_metadata")

grpc_core_end2end_test(name = "graceful_server_shutdown")

grpc_core_end2end_test(name = "grpc_authz")

grpc_core_end2end_test(
    name = "high_initial_seqno",
    shard_count = 20,
)

grpc_core_end2end_test(
    name = "hpack_size",
    shard_count = 50,
)

grpc_core_end2end_test(name = "invoke_large_request")

grpc_core_end2end_test(name = "keepalive_timeout")

grpc_core_end2end_test(
    name = "large_metadata",
    shard_count = 50,
)

grpc_core_end2end_test(name = "max_concurrent_streams")

grpc_core_end2end_test(name = "max_connection_age")

grpc_core_end2end_test(name = "max_connection_idle")

grpc_core_end2end_test(name = "max_message_length")

grpc_core_end2end_test(name = "negative_deadline")

grpc_core_end2end_test(name = "no_logging")

grpc_core_end2end_test(name = "no_op")

grpc_core_end2end_test(name = "payload")

grpc_core_end2end_test(name = "ping")

grpc_core_end2end_test(name = "ping_pong_streaming")

grpc_core_end2end_test(name = "proxy_auth")

grpc_core_end2end_test(name = "registered_call")

grpc_core_end2end_test(
    name = "request_with_flags",
    shard_count = 50,
)

grpc_core_end2end_test(name = "request_with_payload")

grpc_core_end2end_test(name = "resource_quota_server")

grpc_core_end2end_test(name = "retry")

grpc_core_end2end_test(name = "retry_cancel_after_first_attempt_starts")

grpc_core_end2end_test(name = "retry_cancel_during_delay")

grpc_core_end2end_test(name = "retry_cancel_with_multiple_send_batches")

grpc_core_end2end_test(name = "retry_cancellation")

grpc_core_end2end_test(name = "retry_disabled")

grpc_core_end2end_test(name = "retry_exceeds_buffer_size_in_delay")

grpc_core_end2end_test(name = "retry_exceeds_buffer_size_in_initial_batch")

grpc_core_end2end_test(name = "retry_exceeds_buffer_size_in_subsequent_batch")

grpc_core_end2end_test(name = "retry_lb_drop")

grpc_core_end2end_test(name = "retry_lb_fail")

grpc_core_end2end_test(name = "retry_non_retriable_status")

grpc_core_end2end_test(name = "retry_non_retriable_status_before_trailers")

grpc_core_end2end_test(name = "retry_per_attempt_recv_timeout")

grpc_core_end2end_test(name = "retry_per_attempt_recv_timeout_on_last_attempt")

grpc_core_end2end_test(name = "retry_recv_initial_metadata")

grpc_core_end2end_test(name = "retry_recv_message")

grpc_core_end2end_test(name = "retry_recv_message_replay")

grpc_core_end2end_test(name = "retry_recv_trailing_metadata_error")

grpc_core_end2end_test(name = "retry_send_initial_metadata_refs")

grpc_core_end2end_test(name = "retry_send_op_fails")

grpc_core_end2end_test(name = "retry_send_recv_batch")

grpc_core_end2end_test(name = "retry_server_pushback_delay")

grpc_core_end2end_test(name = "retry_server_pushback_disabled")

grpc_core_end2end_test(name = "retry_streaming")

grpc_core_end2end_test(name = "retry_streaming_after_commit")

grpc_core_end2end_test(name = "retry_streaming_succeeds_before_replay_finished")

grpc_core_end2end_test(name = "retry_throttled")

grpc_core_end2end_test(name = "retry_too_many_attempts")

grpc_core_end2end_test(name = "retry_transparent_goaway")

grpc_core_end2end_test(name = "retry_transparent_max_concurrent_streams")

grpc_core_end2end_test(name = "retry_transparent_not_sent_on_wire")

grpc_core_end2end_test(name = "retry_unref_before_finish")

grpc_core_end2end_test(name = "retry_unref_before_recv")

grpc_core_end2end_test(name = "server_finishes_request")

grpc_core_end2end_test(name = "server_streaming")

grpc_core_end2end_test(name = "shutdown_finishes_calls")

grpc_core_end2end_test(name = "shutdown_finishes_tags")

grpc_core_end2end_test(
    name = "simple_delayed_request",
    shard_count = 3,
)

grpc_core_end2end_test(name = "simple_metadata")

grpc_core_end2end_test(name = "simple_request")

grpc_core_end2end_test(name = "streaming_error_response")

grpc_core_end2end_test(name = "trailing_metadata")

grpc_core_end2end_test(name = "write_buffering")

grpc_core_end2end_test(name = "write_buffering_at_end")

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
        "//:channel_arg_names",
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
        "//:channel_arg_names",
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

grpc_cc_test(
    name = "h2_ssl_cert_test",
    srcs = [
        "end2end_tests.h",
        "h2_ssl_cert_test.cc",
    ],
    external_deps = [
        "absl/functional:any_invocable",
        "absl/memory",
        "absl/meta:type_traits",
        "absl/strings",
        "absl/types:optional",
        "absl/types:variant",
        "gtest",
        "libcrypto",
    ],
    language = "C++",
    shard_count = 10,
    deps = [
        "cq_verifier",
        "end2end_test_lib",
        "fixture_support",
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
    external_deps = [
        "absl/types:optional",
        "gtest",
    ],
    language = "C++",
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
    external_deps = [
        "absl/types:optional",
        "gtest",
    ],
    language = "C++",
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
        "//test/core/util:grpc_test_util",
    ],
)

grpc_proto_library(
    name = "end2end_test_fuzzer_proto",
    srcs = ["end2end_test_fuzzer.proto"],
    has_services = False,
    deps = [
        "//test/core/event_engine/fuzzing_event_engine:fuzzing_event_engine_proto",
        "//test/core/util:fuzz_config_vars_proto",
    ],
)
