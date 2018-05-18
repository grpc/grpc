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

POLLERS = ['epollex', 'epollsig', 'epoll1', 'poll', 'poll-cv']

load("//bazel:grpc_build_system.bzl", "grpc_sh_test", "grpc_cc_binary", "grpc_cc_library")

"""Generates the appropriate build.json data for all the end2end tests."""


def fixture_options(fullstack=True, includes_proxy=False, dns_resolver=True,
                    name_resolution=True, secure=True, tracing=False,
                    platforms=['windows', 'linux', 'mac', 'posix'],
                    is_inproc=False, is_http2=True, supports_proxy_auth=False,
                    supports_write_buffering=True, client_channel=True):
  return struct(
    fullstack=fullstack,
    includes_proxy=includes_proxy,
    dns_resolver=dns_resolver,
    name_resolution=name_resolution,
    secure=secure,
    tracing=tracing,
    is_inproc=is_inproc,
    is_http2=is_http2,
    supports_proxy_auth=supports_proxy_auth,
    supports_write_buffering=supports_write_buffering,
    client_channel=client_channel,
    #platforms=platforms,
  )


# maps fixture name to whether it requires the security library
END2END_FIXTURES = {
    'h2_compress': fixture_options(),
    'h2_census': fixture_options(),
    'h2_load_reporting': fixture_options(),
    'h2_fakesec': fixture_options(),
    'h2_fd': fixture_options(dns_resolver=False, fullstack=False,
                             client_channel=False,
                             platforms=['linux', 'mac', 'posix']),
    'h2_full': fixture_options(),
    'h2_full+pipe': fixture_options(platforms=['linux']),
    'h2_full+trace': fixture_options(tracing=True),
    'h2_full+workarounds': fixture_options(),
    'h2_http_proxy': fixture_options(supports_proxy_auth=True),
    'h2_oauth2': fixture_options(),
    'h2_proxy': fixture_options(includes_proxy=True),
    'h2_sockpair_1byte': fixture_options(fullstack=False, dns_resolver=False,
                                         client_channel=False),
    'h2_sockpair': fixture_options(fullstack=False, dns_resolver=False,
                                   client_channel=False),
    'h2_sockpair+trace': fixture_options(fullstack=False, dns_resolver=False,
                                         tracing=True, client_channel=False),
    'h2_ssl': fixture_options(secure=True),
    'h2_ssl_proxy': fixture_options(includes_proxy=True, secure=True),
    'h2_uds': fixture_options(dns_resolver=False,
                              platforms=['linux', 'mac', 'posix']),
    'inproc': fixture_options(fullstack=False, dns_resolver=False,
                              name_resolution=False, is_inproc=True,
                              is_http2=False, supports_write_buffering=False,
                              client_channel=False),
}


def test_options(needs_fullstack=False, needs_dns=False, needs_names=False,
                 proxyable=True, secure=False, traceable=False,
                 exclude_inproc=False, needs_http2=False,
                 needs_proxy_auth=False, needs_write_buffering=False,
                 needs_client_channel=False):
  return struct(
    needs_fullstack=needs_fullstack,
    needs_dns=needs_dns,
    needs_names=needs_names,
    proxyable=proxyable,
    secure=secure,
    traceable=traceable,
    exclude_inproc=exclude_inproc,
    needs_http2=needs_http2,
    needs_proxy_auth=needs_proxy_auth,
    needs_write_buffering=needs_write_buffering,
    needs_client_channel=needs_client_channel,
  )


# maps test names to options
END2END_TESTS = {
    'bad_hostname': test_options(needs_names=True),
    'bad_ping': test_options(needs_fullstack=True,proxyable=False),
    'binary_metadata': test_options(),
    'resource_quota_server': test_options(proxyable=False),
    'call_creds': test_options(secure=True),
    'call_host_override': test_options(needs_fullstack=True, needs_dns=True,
                                       needs_names=True),
    'cancel_after_accept': test_options(),
    'cancel_after_client_done': test_options(),
    'cancel_after_invoke': test_options(),
    'cancel_after_round_trip': test_options(),
    'cancel_before_invoke': test_options(),
    'cancel_in_a_vacuum': test_options(),
    'cancel_with_status': test_options(),
    'compressed_payload': test_options(proxyable=False, exclude_inproc=True),
    'connectivity': test_options(needs_fullstack=True, needs_names=True,
                                 proxyable=False),
    'default_host': test_options(needs_fullstack=True, needs_dns=True,
                                 needs_names=True),
    'disappearing_server': test_options(needs_fullstack=True,needs_names=True),
    'empty_batch': test_options(),
    'filter_causes_close': test_options(),
    'filter_call_init_fails': test_options(),
    'graceful_server_shutdown': test_options(exclude_inproc=True),
    'hpack_size': test_options(proxyable=False, traceable=False,
                               exclude_inproc=True),
    'high_initial_seqno': test_options(),
    'idempotent_request': test_options(),
    'invoke_large_request': test_options(),
    'keepalive_timeout': test_options(proxyable=False, needs_http2=True),
    'large_metadata': test_options(),
    'max_concurrent_streams': test_options(proxyable=False,
                                           exclude_inproc=True),
    'max_connection_age': test_options(exclude_inproc=True),
    'max_connection_idle': test_options(needs_fullstack=True, proxyable=False),
    'max_message_length': test_options(),
    'negative_deadline': test_options(),
    'network_status_change': test_options(),
    'no_logging': test_options(traceable=False),
    'no_op': test_options(),
    'payload': test_options(),
    'load_reporting_hook': test_options(),
    'ping_pong_streaming': test_options(),
    'ping': test_options(needs_fullstack=True, proxyable=False),
    'proxy_auth': test_options(needs_proxy_auth=True),
    'registered_call': test_options(),
    'request_with_flags': test_options(proxyable=False),
    'request_with_payload': test_options(),
    # TODO(roth): Remove proxyable=False for all retry tests once we
    # have a way for the proxy to propagate the fact that trailing
    # metadata is available when initial metadata is returned.
    # See https://github.com/grpc/grpc/issues/14467 for context.
    'retry': test_options(needs_client_channel=True, proxyable=False),
    'retry_cancellation': test_options(needs_client_channel=True,
                                       proxyable=False),
    'retry_disabled': test_options(needs_client_channel=True, proxyable=False),
    'retry_exceeds_buffer_size_in_initial_batch': test_options(
        needs_client_channel=True, proxyable=False),
    'retry_exceeds_buffer_size_in_subsequent_batch': test_options(
        needs_client_channel=True, proxyable=False),
    'retry_non_retriable_status': test_options(needs_client_channel=True,
                                               proxyable=False),
    'retry_non_retriable_status_before_recv_trailing_metadata_started':
        test_options(needs_client_channel=True, proxyable=False),
    'retry_recv_initial_metadata': test_options(needs_client_channel=True,
                                                proxyable=False),
    'retry_recv_message': test_options(needs_client_channel=True,
                                       proxyable=False),
    'retry_server_pushback_delay': test_options(needs_client_channel=True,
                                                proxyable=False),
    'retry_server_pushback_disabled': test_options(needs_client_channel=True,
                                                   proxyable=False),
    'retry_streaming': test_options(needs_client_channel=True, proxyable=False),
    'retry_streaming_after_commit': test_options(needs_client_channel=True,
                                                 proxyable=False),
    'retry_streaming_succeeds_before_replay_finished': test_options(
        needs_client_channel=True, proxyable=False),
    'retry_throttled': test_options(needs_client_channel=True,
                                    proxyable=False),
    'retry_too_many_attempts': test_options(needs_client_channel=True,
                                            proxyable=False),
    'server_finishes_request': test_options(),
    'shutdown_finishes_calls': test_options(),
    'shutdown_finishes_tags': test_options(),
    'simple_cacheable_request': test_options(),
    'simple_delayed_request': test_options(needs_fullstack=True),
    'simple_metadata': test_options(),
    'simple_request': test_options(),
    'streaming_error_response': test_options(),
    'stream_compression_compressed_payload': test_options(proxyable=False,
                                                          exclude_inproc=True),
    'stream_compression_payload': test_options(exclude_inproc=True),
    'stream_compression_ping_pong_streaming': test_options(exclude_inproc=True),
    'trailing_metadata': test_options(),
    'authority_not_supported': test_options(),
    'filter_latency': test_options(),
    'filter_status_code': test_options(),
    'workaround_cronet_compression': test_options(),
    'write_buffering': test_options(needs_write_buffering=True),
    'write_buffering_at_end': test_options(needs_write_buffering=True),
}


def compatible(fopt, topt):
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


def grpc_end2end_tests():
  grpc_cc_library(
    name = 'end2end_tests',
    srcs = ['end2end_tests.cc', 'end2end_test_utils.cc'] + [
             'tests/%s.cc' % t
             for t in sorted(END2END_TESTS.keys())],
    hdrs = [
      'tests/cancel_test_helpers.h',
      'end2end_tests.h'
    ],
    language = "C++",
    deps = [
      ':cq_verifier',
      ':ssl_test_data',
      ':http_proxy',
      ':proxy',
    ]
  )

  for f, fopt in END2END_FIXTURES.items():
    grpc_cc_binary(
      name = '%s_test' % f,
      srcs = ['fixtures/%s.cc' % f],
      language = "C++",
      deps = [
        ':end2end_tests',
        '//test/core/util:grpc_test_util',
        '//:grpc',
        '//test/core/util:gpr_test_util',
        '//:gpr',
      ],
    )
    for t, topt in END2END_TESTS.items():
      #print(compatible(fopt, topt), f, t, fopt, topt)
      if not compatible(fopt, topt): continue
      for poller in POLLERS:
        native.sh_test(
          name = '%s_test@%s@poller=%s' % (f, t, poller),
          data = [':%s_test' % f],
          srcs = ['end2end_test.sh'],
          args = [
            '$(location %s_test)' % f, 
            t,
            poller,
          ],
        )
