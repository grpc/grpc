#!/usr/bin/env python2.7
# Copyright 2015, Google Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.


"""Generates the appropriate build.json data for all the end2end tests."""


def fixture_options(fullstack=True, includes_proxy=False, dns_resolver=True,
                    secure=True, tracing=False,
                    platforms=['windows', 'linux', 'mac', 'posix']):
  return struct(
    fullstack=fullstack,
    includes_proxy=includes_proxy,
    dns_resolver=dns_resolver,
    secure=secure,
    tracing=tracing,
    #platforms=platforms
  )


# maps fixture name to whether it requires the security library
END2END_FIXTURES = {
    'h2_compress': fixture_options(),
    'h2_census': fixture_options(),
    'h2_load_reporting': fixture_options(),
    'h2_fakesec': fixture_options(),
    'h2_fd': fixture_options(dns_resolver=False, fullstack=False,
                             platforms=['linux', 'mac', 'posix']),
    'h2_full': fixture_options(),
    'h2_full+pipe': fixture_options(platforms=['linux']),
    'h2_full+trace': fixture_options(tracing=True),
    'h2_http_proxy': fixture_options(),
    'h2_oauth2': fixture_options(),
    'h2_proxy': fixture_options(includes_proxy=True),
    'h2_sockpair_1byte': fixture_options(fullstack=False, dns_resolver=False),
    'h2_sockpair': fixture_options(fullstack=False, dns_resolver=False),
    'h2_sockpair+trace': fixture_options(fullstack=False, dns_resolver=False,
                                         tracing=True),
    'h2_ssl': fixture_options(secure=True),
    'h2_ssl_cert': fixture_options(secure=True),
    'h2_ssl_proxy': fixture_options(secure=True),
    'h2_uds': fixture_options(dns_resolver=False,
                              platforms=['linux', 'mac', 'posix']),
}


def test_options(needs_fullstack=False, needs_dns=False, proxyable=True,
                 secure=False, traceable=False):
  return struct(
    needs_fullstack=needs_fullstack,
    needs_dns=needs_dns,
    proxyable=proxyable,
    secure=secure,
    traceable=traceable
  )


# maps test names to options
END2END_TESTS = {
    'bad_hostname': test_options(),
    'bad_ping': test_options(),
    'binary_metadata': test_options(),
    'resource_quota_server': test_options(proxyable=False),
    'call_creds': test_options(secure=True),
    'cancel_after_accept': test_options(),
    'cancel_after_client_done': test_options(),
    'cancel_after_invoke': test_options(),
    'cancel_before_invoke': test_options(),
    'cancel_in_a_vacuum': test_options(),
    'cancel_with_status': test_options(),
    'compressed_payload': test_options(),
    'connectivity': test_options(needs_fullstack=True, proxyable=False),
    'default_host': test_options(needs_fullstack=True, needs_dns=True),
    'disappearing_server': test_options(needs_fullstack=True),
    'empty_batch': test_options(),
    'filter_causes_close': test_options(),
    'filter_call_init_fails': test_options(),
    'graceful_server_shutdown': test_options(),
    'hpack_size': test_options(proxyable=False, traceable=False),
    'high_initial_seqno': test_options(),
    'idempotent_request': test_options(),
    'invoke_large_request': test_options(),
    'keepalive_timeout': test_options(proxyable=False),
    'large_metadata': test_options(),
    'max_concurrent_streams': test_options(proxyable=False),
    'max_connection_age': test_options(),
    'max_connection_idle': test_options(needs_fullstack=True, proxyable=False),
    'max_message_length': test_options(),
    'negative_deadline': test_options(),
    'network_status_change': test_options(),
    'no_logging': test_options(traceable=False),
    'no_op': test_options(),
    'payload': test_options(),
    'load_reporting_hook': test_options(),
    'ping_pong_streaming': test_options(),
    'ping': test_options(proxyable=False),
    'registered_call': test_options(),
    'request_with_flags': test_options(proxyable=False),
    'request_with_payload': test_options(),
    'server_finishes_request': test_options(),
    'shutdown_finishes_calls': test_options(),
    'shutdown_finishes_tags': test_options(),
    'simple_cacheable_request': test_options(),
    'simple_delayed_request': test_options(needs_fullstack=True),
    'simple_metadata': test_options(),
    'simple_request': test_options(),
    'streaming_error_response': test_options(),
    'trailing_metadata': test_options(),
    'authority_not_supported': test_options(),
    'filter_latency': test_options(),
    'write_buffering': test_options(),
    'write_buffering_at_end': test_options(),
}


def compatible(fopt, topt):
  if topt.needs_fullstack:
    if not fopt.fullstack:
      return False
  if topt.needs_dns:
    if not fopt.dns_resolver:
      return False
  if not topt.proxyable:
    if fopt.includes_proxy:
      return False
  if not topt.traceable:
    if fopt.tracing:
      return False
  return True


def grpc_end2end_tests():
  native.cc_library(
    name = 'end2end_tests',
    srcs = ['end2end_tests.c', 'end2end_test_utils.c'] + [
             'tests/%s.c' % t
             for t in sorted(END2END_TESTS.keys())],
    hdrs = [
      'tests/cancel_test_helpers.h',
      'end2end_tests.h'
    ],
    copts = ['-std=c99'],
    deps = [
      ':cq_verifier',
      ':ssl_test_data',
      ':fake_resolver',
      ':http_proxy',
      ':proxy',
      '//test/core/util:grpc_test_util',
      '//:grpc',
      '//test/core/util:gpr_test_util',
      '//:gpr',
    ]
  )

  for f, fopt in END2END_FIXTURES.items():
    native.cc_binary(
      name = '%s_test' % f,
      srcs = ['fixtures/%s.c' % f],
      copts = ['-std=c99'],
      deps = [':end2end_tests']
    )
    for t, topt in END2END_TESTS.items():
      #print(compatible(fopt, topt), f, t, fopt, topt)
      if not compatible(fopt, topt): continue
      native.sh_test(
        name = '%s_test@%s' % (f, t),
        srcs = ['end2end_test.sh'],
        args = ['$(location %s_test)' % f, t],
        data = [':%s_test' % f],
      )
