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


FixtureOptions = collections.namedtuple('FixtureOptions', 'fullstack includes_proxy dns_resolver secure platforms ci_mac')
default_unsecure_fixture_options = FixtureOptions(True, False, True, False, ['windows', 'linux', 'mac', 'posix'], True)
socketpair_unsecure_fixture_options = default_unsecure_fixture_options._replace(fullstack=False, dns_resolver=False)
default_secure_fixture_options = default_unsecure_fixture_options._replace(secure=True)
uds_fixture_options = default_unsecure_fixture_options._replace(dns_resolver=False, platforms=['linux', 'mac', 'posix'])


# maps fixture name to whether it requires the security library
END2END_FIXTURES = {
    'h2_compress': default_unsecure_fixture_options,
    'h2_fakesec': default_secure_fixture_options._replace(ci_mac=False),
    'h2_full': default_unsecure_fixture_options,
    'h2_full+poll': default_unsecure_fixture_options._replace(platforms=['linux']),
    'h2_oauth2': default_secure_fixture_options._replace(ci_mac=False),
    'h2_proxy': default_unsecure_fixture_options._replace(includes_proxy=True, ci_mac=False),
    'h2_sockpair_1byte': socketpair_unsecure_fixture_options._replace(ci_mac=False),
    'h2_sockpair': socketpair_unsecure_fixture_options._replace(ci_mac=False),
    'h2_sockpair+trace': socketpair_unsecure_fixture_options,
    'h2_ssl': default_secure_fixture_options,
    'h2_ssl+poll': default_secure_fixture_options._replace(platforms=['linux']),
    'h2_ssl_proxy': default_secure_fixture_options._replace(includes_proxy=True, ci_mac=False),
    'h2_uchannel': default_unsecure_fixture_options,
    'h2_uds+poll': uds_fixture_options._replace(platforms=['linux']),
    'h2_uds': uds_fixture_options,
}

TestOptions = collections.namedtuple('TestOptions', 'needs_fullstack needs_dns proxyable flaky secure')
default_test_options = TestOptions(False, False, True, False, False)
connectivity_test_options = default_test_options._replace(needs_fullstack=True)

# maps test names to options
END2END_TESTS = {
    'bad_hostname': default_test_options,
    'binary_metadata': default_test_options,
    'call_creds': default_test_options._replace(secure=True),
    'cancel_after_accept': default_test_options,
    'cancel_after_client_done': default_test_options,
    'cancel_after_invoke': default_test_options,
    'cancel_before_invoke': default_test_options,
    'cancel_in_a_vacuum': default_test_options,
    'cancel_with_status': default_test_options,
    'census_simple_request': default_test_options,
    'channel_connectivity': connectivity_test_options._replace(proxyable=False),
    'compressed_payload': default_test_options._replace(proxyable=False),
    'default_host': default_test_options._replace(needs_fullstack=True, needs_dns=True),
    'disappearing_server': connectivity_test_options,
    'empty_batch': default_test_options,
    'graceful_server_shutdown': default_test_options,
    'hpack_size': default_test_options,
    'high_initial_seqno': default_test_options,
    'invoke_large_request': default_test_options,
    'large_metadata': default_test_options,
    'max_concurrent_streams': default_test_options._replace(proxyable=False),
    'max_message_length': default_test_options,
    'metadata': default_test_options,
    'negative_deadline': default_test_options,
    'no_op': default_test_options,
    'payload': default_test_options,
    'ping_pong_streaming': default_test_options,
    'registered_call': default_test_options,
    'request_with_flags': default_test_options._replace(proxyable=False),
    'request_with_payload': default_test_options,
    'server_finishes_request': default_test_options,
    'shutdown_finishes_calls': default_test_options,
    'shutdown_finishes_tags': default_test_options,
    'simple_delayed_request': connectivity_test_options,
    'simple_request': default_test_options,
    'trailing_metadata': default_test_options,
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
  return True


def without(l, e):
  l = l[:]
  l.remove(e)
  return l


def main():
  sec_deps = [
    'end2end_certs',
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
              'name': 'end2end_fixture_%s' % f,
              'build': 'private',
              'language': 'c',
              'secure': 'check' if END2END_FIXTURES[f].secure else False,
              'src': ['test/core/end2end/fixtures/%s.c' % f],
              'platforms': [ 'linux', 'mac', 'posix' ] if f.endswith('_posix') else END2END_FIXTURES[f].platforms,
              'deps': sec_deps if END2END_FIXTURES[f].secure else unsec_deps,
              'headers': ['test/core/end2end/end2end_tests.h'],
              'vs_proj_dir': 'test',
          }
          for f in sorted(END2END_FIXTURES.keys())] + [
          {
              'name': 'end2end_test_%s' % t,
              'build': 'private',
              'language': 'c',
              'secure': 'check' if END2END_TESTS[t].secure else False,
              'src': ['test/core/end2end/tests/%s.c' % t],
              'headers': ['test/core/end2end/tests/cancel_test_helpers.h',
                          'test/core/end2end/end2end_tests.h'],
              'deps': sec_deps if END2END_TESTS[t].secure else unsec_deps,
              'vs_proj_dir': 'test',
          }
          for t in sorted(END2END_TESTS.keys())] + [
          {
              'name': 'end2end_certs',
              'build': 'private',
              'language': 'c',
              'src': [
                  "test/core/end2end/data/test_root_cert.c",
                  "test/core/end2end/data/server1_cert.c",
                  "test/core/end2end/data/server1_key.c"
              ],
              'vs_proj_dir': 'test',
          }
          ],
      'targets': [
          {
              'name': '%s_%s_test' % (f, t),
              'build': 'test',
              'language': 'c',
              'src': [],
              'flaky': END2END_TESTS[t].flaky,
              'platforms': END2END_FIXTURES[f].platforms,
              'ci_platforms': (END2END_FIXTURES[f].platforms 
                               if END2END_FIXTURES[f].ci_mac 
                               else without(END2END_FIXTURES[f].platforms, 'mac')),
              'deps': [
                  'end2end_fixture_%s' % f,
                  'end2end_test_%s' % t] + sec_deps,
              'vs_proj_dir': 'test',
          }
      for f in sorted(END2END_FIXTURES.keys())
      for t in sorted(END2END_TESTS.keys())
      if compatible(f, t)] + [
          {
              'name': '%s_%s_nosec_test' % (f, t),
              'build': 'test',
              'language': 'c',
              'secure': 'no',
              'src': [],
              'flaky': END2END_TESTS[t].flaky,
              'platforms': END2END_FIXTURES[f].platforms,
              'ci_platforms': (END2END_FIXTURES[f].platforms 
                               if END2END_FIXTURES[f].ci_mac 
                               else without(END2END_FIXTURES[f].platforms, 'mac')),
              'deps': [
                  'end2end_fixture_%s' % f,
                  'end2end_test_%s' % t] + unsec_deps,
              'vs_proj_dir': 'test',
          }
      for f in sorted(END2END_FIXTURES.keys()) if not END2END_FIXTURES[f].secure
      for t in sorted(END2END_TESTS.keys()) if compatible(f, t) and not END2END_TESTS[t].secure]}
  print yaml.dump(json)


if __name__ == '__main__':
  main()
