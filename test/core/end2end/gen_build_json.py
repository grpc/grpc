#!/usr/bin/env python
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


import simplejson


# maps fixture name to whether it requires the security library
END2END_FIXTURES = {
    'chttp2_fake_security': True,
    'chttp2_fullstack': False,
    'chttp2_fullstack_uds': False,
    'chttp2_simple_ssl_fullstack': True,
    'chttp2_simple_ssl_with_oauth2_fullstack': True,
    'chttp2_socket_pair': False,
    'chttp2_socket_pair_one_byte_at_a_time': False,
}

class TestOptions(object):
  def __init__(self, flaky=False, secure=False):
    self.flaky = flaky
    self.secure = secure

# maps test names to options
END2END_TESTS = {
    'bad_hostname': TestOptions(),
    'cancel_after_accept': TestOptions(flaky=True),
    'cancel_after_accept_and_writes_closed': TestOptions(),
    'cancel_after_invoke': TestOptions(),
    'cancel_before_invoke': TestOptions(),
    'cancel_in_a_vacuum': TestOptions(),
    'census_simple_request': TestOptions(),
    'disappearing_server': TestOptions(),
    'early_server_shutdown_finishes_inflight_calls': TestOptions(),
    'early_server_shutdown_finishes_tags': TestOptions(),
    'empty_batch': TestOptions(),
    'graceful_server_shutdown': TestOptions(),
    'invoke_large_request': TestOptions(flaky=False),
    'max_concurrent_streams': TestOptions(),
    'max_message_length': TestOptions(),
    'no_op': TestOptions(),
    'ping_pong_streaming': TestOptions(),
    'registered_call': TestOptions(),
    'request_response_with_binary_metadata_and_payload': TestOptions(),
    'request_response_with_metadata_and_payload': TestOptions(),
    'request_response_with_payload': TestOptions(),
    'request_response_with_payload_and_call_creds': TestOptions(secure=True),
    'request_with_large_metadata': TestOptions(),
    'request_with_payload': TestOptions(),
    'simple_delayed_request': TestOptions(),
    'simple_request': TestOptions(),
    'simple_request_with_high_initial_sequence_number': TestOptions(),
}


def main():
  json = {
      '#': 'generated with test/end2end/gen_build_json.py',
      'libs': [
          {
              'name': 'end2end_fixture_%s' % f,
              'build': 'private',
              'language': 'c',
              'secure': 'check' if END2END_FIXTURES[f] else 'no',
              'src': ['test/core/end2end/fixtures/%s.c' % f]
          }
          for f in sorted(END2END_FIXTURES.keys())] + [
          {
              'name': 'end2end_test_%s' % t,
              'build': 'private',
              'language': 'c',
              'secure': 'check' if END2END_TESTS[t].secure else 'no',
              'src': ['test/core/end2end/tests/%s.c' % t],
              'headers': ['test/core/end2end/tests/cancel_test_helpers.h']
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
              ]
          }
          ],
      'targets': [
          {
              'name': '%s_%s_test' % (f, t),
              'build': 'test',
              'language': 'c',
              'src': [],
              'flaky': END2END_TESTS[t].flaky,
              'deps': [
                  'end2end_fixture_%s' % f,
                  'end2end_test_%s' % t,
                  'end2end_certs',
                  'grpc_test_util',
                  'grpc',
                  'gpr_test_util',
                  'gpr'
              ]
          }
      for f in sorted(END2END_FIXTURES.keys())
      for t in sorted(END2END_TESTS.keys())] + [
          {
              'name': '%s_%s_unsecure_test' % (f, t),
              'build': 'test',
              'language': 'c',
              'secure': 'no',
              'src': [],
              'flaky': 'invoke_large_request' in t,
              'deps': [
                  'end2end_fixture_%s' % f,
                  'end2end_test_%s' % t,
                  'grpc_test_util_unsecure',
                  'grpc_unsecure',
                  'gpr_test_util',
                  'gpr'
              ]
          }
      for f in sorted(END2END_FIXTURES.keys()) if not END2END_FIXTURES[f]
      for t in sorted(END2END_TESTS.keys()) if not END2END_TESTS[t].secure]}
  print simplejson.dumps(json, sort_keys=True, indent=2 * ' ')


if __name__ == '__main__':
  main()
