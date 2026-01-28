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

"""
Generate one e2e test & associated fuzzer
"""

load("//bazel:grpc_build_system.bzl", "grpc_cc_test")
load(
    "//test/core/test_util:grpc_fuzzer.bzl",
    "grpc_fuzz_test",
)

_DATA = [
    "//src/core/tsi/test_creds:ca.pem",
    "//src/core/tsi/test_creds:server1.key",
    "//src/core/tsi/test_creds:server1.pem",
]

_DEPS = [
    "//test/core/end2end:cq_verifier",
    "//test/core/end2end:http_proxy",
    "//test/core/end2end:proxy",
    "//test/core/call:batch_builder",
    "//:call_combiner",
    "//:call_tracer",
    "//:channel_arg_names",
    "//:channel_stack_builder",
    "//:channelz",
    "//:channel",
    "//:config",
    "//:config_vars",
    "//:debug_location",
    "//:exec_ctx",
    "//:gpr",
    "//:grpc_core_credentials_header",
    "//:grpc_authorization_provider",
    "//:grpc_public_hdrs",
    "//:grpc_security_base",
    "//:grpc_trace",
    "//:grpc_base",
    "//:grpc_transport_chttp2",
    "//:grpc",
    "//:orphanable",
    "//:promise",
    "//:ref_counted_ptr",
    "//:server",
    "//:stats",
    "//src/core:arena_promise",
    "//src/core:call_filters",
    "//src/core:message",
    "//src/core:metadata",
    "//src/core:bitset",
    "//src/core:channel_args",
    "//src/core:channel_fwd",
    "//src/core:channel_init",
    "//src/core:channel_stack_type",
    "//src/core:chaotic_good",
    "//src/core:closure",
    "//src/core:error",
    "//src/core:experiments",
    "//src/core:grpc_authorization_base",
    "//src/core:grpc_check",
    "//src/core:grpc_fake_credentials",
    "//src/core:endpoint_transport",
    "//src/core:iomgr_port",
    "//src/core:json",
    "//src/core:lb_policy",
    "//src/core:lb_policy_factory",
    "//src/core:metadata_batch",
    "//src/core:no_destruct",
    "//src/core:notification",
    "//src/core:slice",
    "//src/core:stats_data",
    "//src/core:status_helper",
    "//src/core:tcp_tracer",
    "//src/core:time",
    "//src/core:unique_type_name",
    "//test/core/test_util:fail_first_call_filter",
    "//test/core/test_util:fake_stats_plugin",
    "//test/core/test_util:grpc_test_util",
    "//test/core/test_util:test_call_creds",
    "//test/core/test_util:test_lb_policies",
    "//test/core/test_util:passthrough_endpoint",
]

_EXTERNAL_DEPS = [
    "absl/base:core_headers",
    "absl/functional:any_invocable",
    "absl/log",
    "absl/log:globals",
    "absl/log:log_entry",
    "absl/log:log_sink",
    "absl/log:log_sink_registry",
    "absl/meta:type_traits",
    "absl/random",
    "absl/status",
    "absl/status:statusor",
    "absl/strings",
    "absl/strings:str_format",
    "absl/time",
    "gtest",
]

_TESTS = [
    "bad_ping",
    "binary_metadata",
    "call_creds",
    "call_host_override",
    "cancel_after_accept",
    "cancel_after_client_done",
    "cancel_after_invoke",
    "cancel_after_round_trip",
    "cancel_before_invoke",
    "cancel_in_a_vacuum",
    "cancel_with_status",
    "channelz",
    "client_streaming",
    "compressed_payload",
    "connectivity",
    "default_host",
    "disappearing_server",
    "empty_batch",
    "filter_causes_close",
    "filter_init_fails",
    "filtered_metadata",
    "graceful_server_shutdown",
    "grpc_authz",
    "high_initial_seqno",
    "hpack_size",
    "http2_stats",
    "invoke_large_request",
    "keepalive_timeout",
    "large_metadata",
    "max_concurrent_streams",
    "max_connection_age",
    "max_connection_idle",
    "max_message_length",
    "negative_deadline",
    "no_op",
    "payload",
    "ping",
    "ping_pong_streaming",
    "proxy_auth",
    "registered_call",
    "request_with_flags",
    "request_with_payload",
    "resource_quota_server",
    "retry",
    "retry_cancel_after_first_attempt_starts",
    "retry_cancel_during_delay",
    "retry_cancel_with_multiple_send_batches",
    "retry_cancellation",
    "retry_disabled",
    "retry_exceeds_buffer_size_in_delay",
    "retry_exceeds_buffer_size_in_initial_batch",
    "retry_exceeds_buffer_size_in_subsequent_batch",
    "retry_lb_drop",
    "retry_lb_fail",
    "retry_non_retriable_status",
    "retry_non_retriable_status_before_trailers",
    "retry_per_attempt_recv_timeout",
    "retry_per_attempt_recv_timeout_on_last_attempt",
    "retry_recv_initial_metadata",
    "retry_recv_message",
    "retry_recv_message_replay",
    "retry_recv_trailing_metadata_error",
    "retry_send_initial_metadata_refs",
    "retry_send_op_fails",
    "retry_send_recv_batch",
    "retry_server_pushback_delay",
    "retry_server_pushback_disabled",
    "retry_streaming",
    "retry_streaming_after_commit",
    "retry_streaming_succeeds_before_replay_finished",
    "retry_throttled",
    "retry_too_many_attempts",
    "retry_transparent_goaway",
    "retry_transparent_max_concurrent_streams",
    "retry_transparent_not_sent_on_wire",
    "retry_unref_before_finish",
    "retry_unref_before_recv",
    "server_finishes_request",
    "server_streaming",
    "shutdown_finishes_calls",
    "shutdown_finishes_tags",
    "simple_delayed_request",
    "simple_metadata",
    "simple_request",
    "streaming_error_response",
    "timeout_before_request_call",
    "trailing_metadata",
    "write_buffering",
    "write_buffering_at_end",
]

def grpc_core_end2end_test_suite(
        name,
        config_src,
        deps = [],
        shard_count = 50,
        enable_fuzzing = True,
        tags = [],
        flaky = False,
        with_no_logging_test = True,
        **kwargs):
    """Generate one core end2end test

    Args:
        name: name of the test, must correspond to a "test/name.cc" file
        config_src: filename of a C++ file that implements the End2endTestConfigs() function
        deps: any additional dependencies needed by config_src provided configurations
        shard_count: per bazel
        tags: per bazel
        flaky: per bazel
        enable_fuzzing: also create a fuzzer
        with_no_logging_test: also create variants with the no_logging test
        **kwargs: per bazel
    """

    if len(name) > 60:
        fail("test name %s too long" % name)

    grpc_cc_test(
        name = name + "_test",
        srcs = [config_src] + [
            "//test/core/end2end:tests/%s.cc" % t
            for t in _TESTS
        ],
        external_deps = _EXTERNAL_DEPS + ["gtest_main"],
        deps = _DEPS + deps + [
            "//test/core/end2end:end2end_test_lib_no_fuzztest_gtest",
        ],
        data = _DATA,
        shard_count = shard_count,
        tags = tags + ["core_end2end_test"],
        flaky = flaky,
        **kwargs
    )

    if enable_fuzzing:
        grpc_fuzz_test(
            name = name + "_fuzzer",
            srcs = [config_src] + [
                "//test/core/end2end:tests/%s.cc" % t
                for t in _TESTS
            ],
            external_deps = _EXTERNAL_DEPS + ["fuzztest", "fuzztest_main"],
            shard_count = shard_count,
            deps = _DEPS + deps + [
                "//test/core/end2end:end2end_test_lib_fuzztest_no_gtest",
            ],
            data = _DATA,
            **kwargs
        )

    if with_no_logging_test:
        grpc_cc_test(
            name = name + "_no_logging_test",
            srcs = [config_src, "//test/core/end2end:tests/no_logging.cc"],
            external_deps = _EXTERNAL_DEPS + ["gtest_main"],
            deps = _DEPS + deps + [
                "//test/core/end2end:end2end_test_lib_no_fuzztest_gtest",
            ],
            data = _DATA,
            shard_count = shard_count,
            tags = tags + ["core_end2end_test", "grpc:fails-internally", "grpc:no-internal-poller"],
            flaky = flaky,
            **kwargs
        )

    if enable_fuzzing and with_no_logging_test:
        grpc_fuzz_test(
            name = name + "_no_logging_fuzzer",
            srcs = [config_src, "//test/core/end2end:tests/no_logging.cc"],
            external_deps = _EXTERNAL_DEPS + ["fuzztest", "fuzztest_main"],
            shard_count = shard_count,
            deps = _DEPS + deps + [
                "//test/core/end2end:end2end_test_lib_fuzztest_no_gtest",
            ],
            data = _DATA,
            **kwargs
        )
