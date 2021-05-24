#!/usr/bin/env python2.7
# Copyright 2015 gRPC authors.
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

"""Generates the appropriate build.json data for all the end2end tests."""

load("//bazel:grpc_build_system.bzl", "grpc_cc_binary", "grpc_cc_library")

POLLERS = ["epollex", "epoll1", "poll"]

def _fixture_options(
        fullstack = True,
        includes_proxy = False,
        dns_resolver = True,
        name_resolution = True,
        secure = True,
        tracing = False,
        _platforms = ["windows", "linux", "mac", "posix"],
        is_inproc = False,
        is_http2 = True,
        supports_proxy_auth = False,
        supports_write_buffering = True,
        client_channel = True,
        supports_msvc = True,
        flaky_tests = []):
    return struct(
        fullstack = fullstack,
        includes_proxy = includes_proxy,
        dns_resolver = dns_resolver,
        name_resolution = name_resolution,
        secure = secure,
        tracing = tracing,
        is_inproc = is_inproc,
        is_http2 = is_http2,
        supports_proxy_auth = supports_proxy_auth,
        supports_write_buffering = supports_write_buffering,
        client_channel = client_channel,
        supports_msvc = supports_msvc,
        _platforms = _platforms,
        flaky_tests = flaky_tests,
    )

# maps fixture name to whether it requires the security library
END2END_FIXTURES = {
    "h2_compress": _fixture_options(),
    "h2_census": _fixture_options(),
    # TODO(juanlishen): This is disabled for now, but should be considered to re-enable once we have
    # decided how the load reporting service should be enabled.
    #'h2_load_reporting': _fixture_options(),
    "h2_fakesec": _fixture_options(),
    "h2_fd": _fixture_options(
        dns_resolver = False,
        fullstack = False,
        client_channel = False,
        _platforms = ["linux", "mac", "posix"],
    ),
    "h2_full": _fixture_options(),
    "h2_full+pipe": _fixture_options(_platforms = ["linux"]),
    "h2_full+trace": _fixture_options(tracing = True),
    "h2_full+workarounds": _fixture_options(),
    "h2_http_proxy": _fixture_options(supports_proxy_auth = True),
    "h2_insecure": _fixture_options(secure = True),
    "h2_oauth2": _fixture_options(),
    "h2_proxy": _fixture_options(includes_proxy = True),
    "h2_sockpair_1byte": _fixture_options(
        fullstack = False,
        dns_resolver = False,
        client_channel = False,
    ),
    "h2_sockpair": _fixture_options(
        fullstack = False,
        dns_resolver = False,
        client_channel = False,
    ),
    "h2_sockpair+trace": _fixture_options(
        fullstack = False,
        dns_resolver = False,
        tracing = True,
        client_channel = False,
    ),
    "h2_ssl": _fixture_options(secure = True),
    "h2_ssl_cred_reload": _fixture_options(secure = True),
    "h2_tls": _fixture_options(secure = True),
    "h2_local_uds": _fixture_options(
        secure = True,
        dns_resolver = False,
        _platforms = ["linux", "mac", "posix"],
    ),
    "h2_local_ipv4": _fixture_options(secure = True, dns_resolver = False, _platforms = ["linux", "mac", "posix"]),
    "h2_local_ipv6": _fixture_options(secure = True, dns_resolver = False, _platforms = ["linux", "mac", "posix"]),
    "h2_ssl_proxy": _fixture_options(includes_proxy = True, secure = True),
    "h2_uds": _fixture_options(
        dns_resolver = False,
        _platforms = ["linux", "mac", "posix"],
    ),
    "inproc": _fixture_options(
        secure = True,
        fullstack = False,
        dns_resolver = False,
        name_resolution = False,
        is_inproc = True,
        is_http2 = False,
        supports_write_buffering = False,
        client_channel = False,
    ),
}

# maps fixture name to whether it requires the security library
END2END_NOSEC_FIXTURES = {
    "h2_compress": _fixture_options(secure = False),
    "h2_census": _fixture_options(secure = False),
    # TODO(juanlishen): This is disabled for now, but should be considered to re-enable once we have
    # decided how the load reporting service should be enabled.
    #'h2_load_reporting': _fixture_options(),
    "h2_fakesec": _fixture_options(),
    "h2_fd": _fixture_options(
        dns_resolver = False,
        fullstack = False,
        client_channel = False,
        secure = False,
        _platforms = ["linux", "mac", "posix"],
        supports_msvc = False,
    ),
    "h2_full": _fixture_options(secure = False),
    "h2_full+pipe": _fixture_options(secure = False, _platforms = ["linux"], supports_msvc = False),
    "h2_full+trace": _fixture_options(secure = False, tracing = True, supports_msvc = False),
    "h2_full+workarounds": _fixture_options(secure = False),
    "h2_http_proxy": _fixture_options(secure = False, supports_proxy_auth = True),
    "h2_proxy": _fixture_options(secure = False, includes_proxy = True),
    "h2_sockpair_1byte": _fixture_options(
        fullstack = False,
        dns_resolver = False,
        client_channel = False,
        secure = False,
    ),
    "h2_sockpair": _fixture_options(
        fullstack = False,
        dns_resolver = False,
        client_channel = False,
        secure = False,
    ),
    "h2_sockpair+trace": _fixture_options(
        fullstack = False,
        dns_resolver = False,
        tracing = True,
        secure = False,
        client_channel = False,
    ),
    "h2_ssl": _fixture_options(secure = False),
    "h2_ssl_cred_reload": _fixture_options(secure = False),
    "h2_ssl_proxy": _fixture_options(includes_proxy = True, secure = False),
    "h2_uds": _fixture_options(
        dns_resolver = False,
        _platforms = ["linux", "mac", "posix"],
        secure = False,
        supports_msvc = False,
    ),
}

def _test_options(
        needs_fullstack = False,
        needs_dns = False,
        needs_names = False,
        proxyable = True,
        secure = False,
        traceable = False,
        exclude_inproc = False,
        needs_http2 = False,
        needs_proxy_auth = False,
        needs_write_buffering = False,
        needs_client_channel = False,
        short_name = None,
        exclude_pollers = []):
    return struct(
        needs_fullstack = needs_fullstack,
        needs_dns = needs_dns,
        needs_names = needs_names,
        proxyable = proxyable,
        secure = secure,
        traceable = traceable,
        exclude_inproc = exclude_inproc,
        needs_http2 = needs_http2,
        needs_proxy_auth = needs_proxy_auth,
        needs_write_buffering = needs_write_buffering,
        needs_client_channel = needs_client_channel,
        short_name = short_name,
        exclude_pollers = exclude_pollers,
    )

# maps test names to options
END2END_TESTS = {
    "bad_hostname": _test_options(needs_names = True),
    "bad_ping": _test_options(needs_fullstack = True, proxyable = False),
    "binary_metadata": _test_options(),
    "resource_quota_server": _test_options(
        proxyable = False,
        # TODO(b/151212019): Test case known to be flaky under epoll1.
        exclude_pollers = ["epoll1"],
    ),
    "call_creds": _test_options(secure = True),
    "call_host_override": _test_options(
        needs_fullstack = True,
        needs_dns = True,
        needs_names = True,
    ),
    "cancel_after_accept": _test_options(),
    "cancel_after_client_done": _test_options(),
    "cancel_after_invoke": _test_options(),
    "cancel_after_round_trip": _test_options(),
    "cancel_before_invoke": _test_options(),
    "cancel_in_a_vacuum": _test_options(),
    "cancel_with_status": _test_options(),
    "client_streaming": _test_options(),
    "compressed_payload": _test_options(proxyable = False, exclude_inproc = True),
    "connectivity": _test_options(
        needs_fullstack = True,
        needs_names = True,
        proxyable = False,
    ),
    "channelz": _test_options(),
    "default_host": _test_options(
        needs_fullstack = True,
        needs_dns = True,
        needs_names = True,
    ),
    "disappearing_server": _test_options(needs_fullstack = True, needs_names = True),
    "empty_batch": _test_options(),
    "filter_causes_close": _test_options(),
    "filter_init_fails": _test_options(),
    "filter_context": _test_options(),
    "graceful_server_shutdown": _test_options(exclude_inproc = True),
    "hpack_size": _test_options(
        proxyable = False,
        traceable = False,
        exclude_inproc = True,
    ),
    "high_initial_seqno": _test_options(),
    "idempotent_request": _test_options(),
    "invoke_large_request": _test_options(),
    "keepalive_timeout": _test_options(proxyable = False, needs_http2 = True),
    "large_metadata": _test_options(),
    "max_concurrent_streams": _test_options(
        proxyable = False,
        exclude_inproc = True,
    ),
    "max_connection_age": _test_options(exclude_inproc = True),
    "max_connection_idle": _test_options(needs_fullstack = True, proxyable = False),
    "max_message_length": _test_options(),
    "negative_deadline": _test_options(),
    "no_error_on_hotpath": _test_options(proxyable = False),
    "no_logging": _test_options(traceable = False),
    "no_op": _test_options(),
    "payload": _test_options(),
    # TODO(juanlishen): This is disabled for now because it depends on some generated functions in
    # end2end_tests.cc, which are not generated because they would depend on OpenCensus while
    # OpenCensus can only be built via Bazel so far.
    # 'load_reporting_hook': _test_options(),
    "ping_pong_streaming": _test_options(),
    "ping": _test_options(needs_fullstack = True, proxyable = False),
    "proxy_auth": _test_options(needs_proxy_auth = True),
    "registered_call": _test_options(),
    "request_with_flags": _test_options(proxyable = False),
    "request_with_payload": _test_options(),
    # TODO(roth): Remove proxyable=False for all retry tests once we
    # have a way for the proxy to propagate the fact that trailing
    # metadata is available when initial metadata is returned.
    # See https://github.com/grpc/grpc/issues/14467 for context.
    "retry": _test_options(needs_client_channel = True, proxyable = False),
    "retry_cancellation": _test_options(
        needs_client_channel = True,
        proxyable = False,
    ),
    "retry_cancel_during_delay": _test_options(
        needs_client_channel = True,
        proxyable = False,
    ),
    "retry_disabled": _test_options(needs_client_channel = True, proxyable = False),
    "retry_exceeds_buffer_size_in_initial_batch": _test_options(
        needs_client_channel = True,
        proxyable = False,
        # TODO(jtattermusch): too long bazel test name makes the test flaky on Windows RBE
        # See b/151617965
        short_name = "retry_exceeds_buffer_size_in_init",
    ),
    "retry_exceeds_buffer_size_in_subsequent_batch": _test_options(
        needs_client_channel = True,
        proxyable = False,
        # TODO(jtattermusch): too long bazel test name makes the test flaky on Windows RBE
        # See b/151617965
        short_name = "retry_exceeds_buffer_size_in_subseq",
    ),
    "retry_lb_drop": _test_options(
        needs_client_channel = True,
        proxyable = False,
    ),
    "retry_non_retriable_status": _test_options(
        needs_client_channel = True,
        proxyable = False,
    ),
    "retry_non_retriable_status_before_recv_trailing_metadata_started": _test_options(
        needs_client_channel = True,
        proxyable = False,
        # TODO(jtattermusch): too long bazel test name makes the test flaky on Windows RBE
        # See b/151617965
        short_name = "retry_non_retriable_status2",
    ),
    "retry_recv_initial_metadata": _test_options(
        needs_client_channel = True,
        proxyable = False,
    ),
    "retry_recv_message": _test_options(
        needs_client_channel = True,
        proxyable = False,
    ),
    "retry_server_pushback_delay": _test_options(
        needs_client_channel = True,
        proxyable = False,
    ),
    "retry_server_pushback_disabled": _test_options(
        needs_client_channel = True,
        proxyable = False,
    ),
    "retry_streaming": _test_options(needs_client_channel = True, proxyable = False),
    "retry_streaming_after_commit": _test_options(
        needs_client_channel = True,
        proxyable = False,
    ),
    "retry_streaming_succeeds_before_replay_finished": _test_options(
        needs_client_channel = True,
        proxyable = False,
        # TODO(jtattermusch): too long bazel test name makes the test flaky on Windows RBE
        # See b/151617965
        short_name = "retry_streaming2",
    ),
    "retry_throttled": _test_options(
        needs_client_channel = True,
        proxyable = False,
    ),
    "retry_too_many_attempts": _test_options(
        needs_client_channel = True,
        proxyable = False,
    ),
    "server_finishes_request": _test_options(),
    "server_streaming": _test_options(needs_http2 = True),
    "shutdown_finishes_calls": _test_options(),
    "shutdown_finishes_tags": _test_options(),
    "simple_cacheable_request": _test_options(),
    "simple_delayed_request": _test_options(needs_fullstack = True),
    "simple_metadata": _test_options(),
    "simple_request": _test_options(),
    "streaming_error_response": _test_options(),
    "stream_compression_compressed_payload": _test_options(
        proxyable = False,
        exclude_inproc = True,
    ),
    "stream_compression_payload": _test_options(exclude_inproc = True),
    "stream_compression_ping_pong_streaming": _test_options(exclude_inproc = True),
    "trailing_metadata": _test_options(),
    "authority_not_supported": _test_options(),
    "filter_latency": _test_options(),
    "filter_status_code": _test_options(),
    "workaround_cronet_compression": _test_options(),
    "write_buffering": _test_options(needs_write_buffering = True),
    "write_buffering_at_end": _test_options(needs_write_buffering = True),
}

def _compatible(fopt, topt):
    if topt.needs_fullstack:
        if not fopt.fullstack:
            return False
    if topt.needs_dns:
        if not fopt.dns_resolver:
            return False
    if topt.needs_names:
        if not fopt.name_resolution:
            return False
    if not topt.proxyable:
        if fopt.includes_proxy:
            return False
    if not topt.traceable:
        if fopt.tracing:
            return False
    if topt.exclude_inproc:
        if fopt.is_inproc:
            return False
    if topt.needs_http2:
        if not fopt.is_http2:
            return False
    if topt.needs_proxy_auth:
        if not fopt.supports_proxy_auth:
            return False
    if topt.needs_write_buffering:
        if not fopt.supports_write_buffering:
            return False
    if topt.needs_client_channel:
        if not fopt.client_channel:
            return False
    return True

def _platform_support_tags(fopt):
    result = []
    if not "windows" in fopt._platforms:
        result += ["no_windows"]
    if not "mac" in fopt._platforms:
        result += ["no_mac"]
    if not "linux" in fopt._platforms:
        result += ["no_linux"]
    return result

def grpc_end2end_tests():
    grpc_cc_library(
        name = "end2end_tests",
        srcs = ["end2end_tests.cc", "end2end_test_utils.cc"] + [
            "tests/%s.cc" % t
            for t in sorted(END2END_TESTS.keys())
        ],
        hdrs = [
            "tests/cancel_test_helpers.h",
            "end2end_tests.h",
        ],
        language = "C++",
        testonly = 1,
        deps = [
            ":cq_verifier",
            ":ssl_test_data",
            ":http_proxy",
            ":proxy",
            ":local_util",
            "//test/core/util:test_lb_policies",
        ],
    )

    for f, fopt in END2END_FIXTURES.items():
        grpc_cc_binary(
            name = "%s_test" % f,
            srcs = ["fixtures/%s.cc" % f],
            language = "C++",
            testonly = 1,
            data = [
                "//src/core/tsi/test_creds:ca.pem",
                "//src/core/tsi/test_creds:server1.key",
                "//src/core/tsi/test_creds:server1.pem",
            ],
            deps = [
                ":end2end_tests",
                "//test/core/util:grpc_test_util",
                "//:grpc",
                "//:gpr",
            ],
            tags = _platform_support_tags(fopt),
        )

        for t, topt in END2END_TESTS.items():
            #print(_compatible(fopt, topt), f, t, fopt, topt)
            if not _compatible(fopt, topt):
                continue

            test_short_name = str(t) if not topt.short_name else topt.short_name
            native.sh_test(
                name = "%s_test@%s" % (f, test_short_name),
                data = [":%s_test" % f],
                srcs = ["end2end_test.sh"],
                args = [
                    "$(location %s_test)" % f,
                    t,
                ],
                tags = ["no_linux"] + _platform_support_tags(fopt),
                flaky = t in fopt.flaky_tests,
            )

            for poller in POLLERS:
                if poller in topt.exclude_pollers:
                    continue
                native.sh_test(
                    name = "%s_test@%s@poller=%s" % (f, test_short_name, poller),
                    data = [":%s_test" % f],
                    srcs = ["end2end_test.sh"],
                    args = [
                        "$(location %s_test)" % f,
                        t,
                        poller,
                    ],
                    tags = ["no_mac", "no_windows"],
                    flaky = t in fopt.flaky_tests,
                )

def grpc_end2end_nosec_tests():
    grpc_cc_library(
        name = "end2end_nosec_tests",
        srcs = ["end2end_nosec_tests.cc", "end2end_test_utils.cc"] + [
            "tests/%s.cc" % t
            for t in sorted(END2END_TESTS.keys())
            if not END2END_TESTS[t].secure
        ],
        hdrs = [
            "tests/cancel_test_helpers.h",
            "end2end_tests.h",
        ],
        language = "C++",
        testonly = 1,
        deps = [
            ":cq_verifier",
            ":ssl_test_data",
            ":http_proxy",
            ":proxy",
            ":local_util",
            "//test/core/util:test_lb_policies",
        ],
    )

    for f, fopt in END2END_NOSEC_FIXTURES.items():
        if fopt.secure:
            continue
        grpc_cc_binary(
            name = "%s_nosec_test" % f,
            srcs = ["fixtures/%s.cc" % f],
            language = "C++",
            testonly = 1,
            data = [
                "//src/core/tsi/test_creds:ca.pem",
                "//src/core/tsi/test_creds:server1.key",
                "//src/core/tsi/test_creds:server1.pem",
            ],
            deps = [
                ":end2end_nosec_tests",
                "//test/core/util:grpc_test_util_unsecure",
                "//:grpc_unsecure",
                "//:gpr",
            ],
            tags = _platform_support_tags(fopt),
        )
        for t, topt in END2END_TESTS.items():
            #print(_compatible(fopt, topt), f, t, fopt, topt)
            if not _compatible(fopt, topt):
                continue
            if topt.secure:
                continue

            test_short_name = str(t) if not topt.short_name else topt.short_name
            native.sh_test(
                name = "%s_nosec_test@%s" % (f, test_short_name),
                data = [":%s_nosec_test" % f],
                srcs = ["end2end_test.sh"],
                args = [
                    "$(location %s_nosec_test)" % f,
                    t,
                ],
                tags = ["no_linux"] + _platform_support_tags(fopt),
                flaky = t in fopt.flaky_tests,
            )

            for poller in POLLERS:
                if poller in topt.exclude_pollers:
                    continue
                native.sh_test(
                    name = "%s_nosec_test@%s@poller=%s" % (f, test_short_name, poller),
                    data = [":%s_nosec_test" % f],
                    srcs = ["end2end_test.sh"],
                    args = [
                        "$(location %s_nosec_test)" % f,
                        t,
                        poller,
                    ],
                    tags = ["no_mac", "no_windows"],
                    flaky = t in fopt.flaky_tests,
                )
