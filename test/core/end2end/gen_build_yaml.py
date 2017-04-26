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


import yaml
import collections
import hashlib


FixtureOptions = collections.namedtuple(
    'FixtureOptions',
    'fullstack includes_proxy dns_resolver secure platforms ci_mac tracing exclude_configs exclude_iomgrs large_writes')
default_unsecure_fixture_options = FixtureOptions(
    True, False, True, False, ['windows', 'linux', 'mac', 'posix'], True, False, [], [], True)
socketpair_unsecure_fixture_options = default_unsecure_fixture_options._replace(fullstack=False, dns_resolver=False)
default_secure_fixture_options = default_unsecure_fixture_options._replace(secure=True)
uds_fixture_options = default_unsecure_fixture_options._replace(dns_resolver=False, platforms=['linux', 'mac', 'posix'], exclude_iomgrs=['uv'])
fd_unsecure_fixture_options = default_unsecure_fixture_options._replace(
    dns_resolver=False, fullstack=False, platforms=['linux', 'mac', 'posix'], exclude_iomgrs=['uv'])


# maps fixture name to whether it requires the security library
END2END_FIXTURES = {
    'h2_compress': default_unsecure_fixture_options,

    'h2_census': default_unsecure_fixture_options,
    'h2_load_reporting': default_unsecure_fixture_options,
    'h2_fakesec': default_secure_fixture_options._replace(ci_mac=False),
    'h2_fd': fd_unsecure_fixture_options,
    'h2_full': default_unsecure_fixture_options,
    'h2_full+pipe': default_unsecure_fixture_options._replace(
        platforms=['linux'], exclude_iomgrs=['uv']),
    'h2_full+trace': default_unsecure_fixture_options._replace(tracing=True),
    'h2_http_proxy': default_unsecure_fixture_options._replace(
        ci_mac=False, exclude_iomgrs=['uv']),
    'h2_oauth2': default_secure_fixture_options._replace(
        ci_mac=False, exclude_iomgrs=['uv']),
    'h2_proxy': default_unsecure_fixture_options._replace(
        includes_proxy=True, ci_mac=False, exclude_iomgrs=['uv']),
    'h2_sockpair_1byte': socketpair_unsecure_fixture_options._replace(
        ci_mac=False, exclude_configs=['msan'], large_writes=False,
        exclude_iomgrs=['uv']),
    'h2_sockpair': socketpair_unsecure_fixture_options._replace(
        ci_mac=False, exclude_iomgrs=['uv']),
    'h2_sockpair+trace': socketpair_unsecure_fixture_options._replace(
        ci_mac=False, tracing=True, large_writes=False, exclude_iomgrs=['uv']),
    'h2_ssl': default_secure_fixture_options,
    'h2_ssl_cert': default_secure_fixture_options,
    'h2_ssl_proxy': default_secure_fixture_options._replace(
        includes_proxy=True, ci_mac=False, exclude_iomgrs=['uv']),
    'h2_uds': uds_fixture_options,
}

TestOptions = collections.namedtuple(
    'TestOptions',
    'needs_fullstack needs_dns proxyable secure traceable cpu_cost exclude_iomgrs large_writes flaky')
default_test_options = TestOptions(False, False, True, False, True, 1.0, [], False, False)
connectivity_test_options = default_test_options._replace(needs_fullstack=True)

LOWCPU = 0.1

# maps test names to options
END2END_TESTS = {
    'authority_not_supported': default_test_options,
    'bad_hostname': default_test_options,
    'bad_ping': connectivity_test_options._replace(proxyable=False),
    'binary_metadata': default_test_options,
    'resource_quota_server': default_test_options._replace(large_writes=True,
                                                           proxyable=False),
    'call_creds': default_test_options._replace(secure=True),
    'cancel_after_accept': default_test_options._replace(cpu_cost=LOWCPU),
    'cancel_after_client_done': default_test_options,
    'cancel_after_invoke': default_test_options._replace(cpu_cost=LOWCPU),
    'cancel_before_invoke': default_test_options._replace(cpu_cost=LOWCPU),
    'cancel_in_a_vacuum': default_test_options._replace(cpu_cost=LOWCPU),
    'cancel_with_status': default_test_options._replace(cpu_cost=LOWCPU),
    'compressed_payload': default_test_options._replace(proxyable=False),
    'connectivity': connectivity_test_options._replace(
        proxyable=False, cpu_cost=LOWCPU, exclude_iomgrs=['uv']),
    'default_host': default_test_options._replace(needs_fullstack=True,
                                                  needs_dns=True),
    'disappearing_server': connectivity_test_options._replace(flaky=True),
    'empty_batch': default_test_options,
    'filter_causes_close': default_test_options,
    'filter_call_init_fails': default_test_options,
    'filter_latency': default_test_options,
    'graceful_server_shutdown': default_test_options._replace(cpu_cost=LOWCPU),
    'hpack_size': default_test_options._replace(proxyable=False,
                                                traceable=False),
    'high_initial_seqno': default_test_options,
    'idempotent_request': default_test_options,
    'invoke_large_request': default_test_options,
    'keepalive_timeout': default_test_options._replace(proxyable=False),
    'large_metadata': default_test_options,
    'max_concurrent_streams': default_test_options._replace(proxyable=False),
    'max_connection_age': default_test_options,
    'max_connection_idle': connectivity_test_options._replace(
        proxyable=False, exclude_iomgrs=['uv']),
    'max_message_length': default_test_options,
    'negative_deadline': default_test_options,
    'network_status_change': default_test_options,
    'no_logging': default_test_options._replace(traceable=False),
    'no_op': default_test_options,
    'payload': default_test_options,
    'load_reporting_hook': default_test_options,
    'ping_pong_streaming': default_test_options,
    'ping': connectivity_test_options._replace(proxyable=False),
    'registered_call': default_test_options,
    'request_with_flags': default_test_options._replace(
        proxyable=False, cpu_cost=LOWCPU),
    'request_with_payload': default_test_options,
    'server_finishes_request': default_test_options,
    'shutdown_finishes_calls': default_test_options,
    'shutdown_finishes_tags': default_test_options,
    'simple_cacheable_request': default_test_options,
    'simple_delayed_request': connectivity_test_options,
    'simple_metadata': default_test_options,
    'simple_request': default_test_options,
    'streaming_error_response': default_test_options,
    'trailing_metadata': default_test_options,
    'write_buffering': default_test_options,
    'write_buffering_at_end': default_test_options,
}


def compatible(f, t):
  if END2END_TESTS[t].needs_fullstack:
    if not END2END_FIXTURES[f].fullstack:
      return False
  if END2END_TESTS[t].needs_dns:
    if not END2END_FIXTURES[f].dns_resolver:
      return False
  if not END2END_TESTS[t].proxyable:
    if END2END_FIXTURES[f].includes_proxy:
      return False
  if not END2END_TESTS[t].traceable:
    if END2END_FIXTURES[f].tracing:
      return False
  if END2END_TESTS[t].large_writes:
    if not END2END_FIXTURES[f].large_writes:
      return False
  return True


def without(l, e):
  l = l[:]
  l.remove(e)
  return l


def main():
  sec_deps = [
    'grpc_test_util',
    'grpc',
    'gpr_test_util',
    'gpr'
  ]
  unsec_deps = [
    'grpc_test_util_unsecure',
    'grpc_unsecure',
    'gpr_test_util',
    'gpr'
  ]
  json = {
      '#': 'generated with test/end2end/gen_build_json.py',
      'libs': [
          {
              'name': 'end2end_tests',
              'build': 'private',
              'language': 'c',
              'secure': True,
              'src': ['test/core/end2end/end2end_tests.c',
                      'test/core/end2end/end2end_test_utils.c'] + [
                  'test/core/end2end/tests/%s.c' % t
                  for t in sorted(END2END_TESTS.keys())],
              'headers': ['test/core/end2end/tests/cancel_test_helpers.h',
                          'test/core/end2end/end2end_tests.h'],
              'deps': sec_deps,
              'vs_proj_dir': 'test/end2end/tests',
          }
      ] + [
          {
              'name': 'end2end_nosec_tests',
              'build': 'private',
              'language': 'c',
              'secure': False,
              'src': ['test/core/end2end/end2end_nosec_tests.c',
                      'test/core/end2end/end2end_test_utils.c'] + [
                  'test/core/end2end/tests/%s.c' % t
                  for t in sorted(END2END_TESTS.keys())
                  if not END2END_TESTS[t].secure],
              'headers': ['test/core/end2end/tests/cancel_test_helpers.h',
                          'test/core/end2end/end2end_tests.h'],
              'deps': unsec_deps,
              'vs_proj_dir': 'test/end2end/tests',
          }
      ],
      'targets': [
          {
              'name': '%s_test' % f,
              'build': 'test',
              'language': 'c',
              'run': False,
              'src': ['test/core/end2end/fixtures/%s.c' % f],
              'platforms': END2END_FIXTURES[f].platforms,
              'ci_platforms': (END2END_FIXTURES[f].platforms
                               if END2END_FIXTURES[f].ci_mac else without(
                                   END2END_FIXTURES[f].platforms, 'mac')),
              'deps': [
                  'end2end_tests'
              ] + sec_deps,
              'vs_proj_dir': 'test/end2end/fixtures',
          }
          for f in sorted(END2END_FIXTURES.keys())
      ] + [
          {
              'name': '%s_nosec_test' % f,
              'build': 'test',
              'language': 'c',
              'secure': False,
              'src': ['test/core/end2end/fixtures/%s.c' % f],
              'run': False,
              'platforms': END2END_FIXTURES[f].platforms,
              'ci_platforms': (END2END_FIXTURES[f].platforms
                               if END2END_FIXTURES[f].ci_mac else without(
                                   END2END_FIXTURES[f].platforms, 'mac')),
              'deps': [
                  'end2end_nosec_tests'
              ] + unsec_deps,
              'vs_proj_dir': 'test/end2end/fixtures',
          }
          for f in sorted(END2END_FIXTURES.keys())
          if not END2END_FIXTURES[f].secure
      ],
      'tests': [
          {
              'name': '%s_test' % f,
              'args': [t],
              'exclude_configs': END2END_FIXTURES[f].exclude_configs,
              'exclude_iomgrs': list(set(END2END_FIXTURES[f].exclude_iomgrs) |
                                     set(END2END_TESTS[t].exclude_iomgrs)),
              'platforms': END2END_FIXTURES[f].platforms,
              'ci_platforms': (END2END_FIXTURES[f].platforms
                               if END2END_FIXTURES[f].ci_mac else without(
                                   END2END_FIXTURES[f].platforms, 'mac')),
              'flaky': END2END_TESTS[t].flaky,
              'language': 'c',
              'cpu_cost': END2END_TESTS[t].cpu_cost,
          }
          for f in sorted(END2END_FIXTURES.keys())
          for t in sorted(END2END_TESTS.keys()) if compatible(f, t)
      ] + [
          {
              'name': '%s_nosec_test' % f,
              'args': [t],
              'exclude_configs': END2END_FIXTURES[f].exclude_configs,
              'exclude_iomgrs': list(set(END2END_FIXTURES[f].exclude_iomgrs) |
                                     set(END2END_TESTS[t].exclude_iomgrs)),
              'platforms': END2END_FIXTURES[f].platforms,
              'ci_platforms': (END2END_FIXTURES[f].platforms
                               if END2END_FIXTURES[f].ci_mac else without(
                                   END2END_FIXTURES[f].platforms, 'mac')),
              'flaky': END2END_TESTS[t].flaky,
              'language': 'c',
              'cpu_cost': END2END_TESTS[t].cpu_cost,
          }
          for f in sorted(END2END_FIXTURES.keys())
          if not END2END_FIXTURES[f].secure
          for t in sorted(END2END_TESTS.keys())
          if compatible(f, t) and not END2END_TESTS[t].secure
      ],
      'core_end2end_tests': dict(
          (t, END2END_TESTS[t].secure)
          for t in END2END_TESTS.keys()
      )
  }
  print yaml.dump(json)


if __name__ == '__main__':
  main()
