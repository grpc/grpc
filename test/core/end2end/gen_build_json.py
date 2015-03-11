#!/usr/bin/python2.7
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

END2END_FIXTURES = [
    'chttp2_fake_security',
    'chttp2_fullstack',
    'chttp2_fullstack_uds',
    'chttp2_simple_ssl_fullstack',
    'chttp2_simple_ssl_with_oauth2_fullstack',
    'chttp2_socket_pair',
    'chttp2_socket_pair_one_byte_at_a_time',
]


END2END_TESTS = [
    'bad_hostname',
    'cancel_after_accept',
    'cancel_after_accept_and_writes_closed',
    'cancel_after_invoke',
    'cancel_before_invoke',
    'cancel_in_a_vacuum',
    'census_simple_request',
    'disappearing_server',
    'early_server_shutdown_finishes_inflight_calls',
    'early_server_shutdown_finishes_tags',
    'empty_batch',
    'graceful_server_shutdown',
    'invoke_large_request',
    'max_concurrent_streams',
    'no_op',
    'ping_pong_streaming',
    'request_response_with_binary_metadata_and_payload',
    'request_response_with_metadata_and_payload',
    'request_response_with_payload',
    'request_with_large_metadata',
    'request_with_payload',
    'simple_delayed_request',
    'simple_request',
    'thread_stress',
    'writes_done_hangs_with_pending_read',

    'cancel_after_accept_legacy',
    'cancel_after_accept_and_writes_closed_legacy',
    'cancel_after_invoke_legacy',
    'cancel_before_invoke_legacy',
    'cancel_in_a_vacuum_legacy',
    'census_simple_request_legacy',
    'disappearing_server_legacy',
    'early_server_shutdown_finishes_inflight_calls_legacy',
    'early_server_shutdown_finishes_tags_legacy',
    'graceful_server_shutdown_legacy',
    'invoke_large_request_legacy',
    'max_concurrent_streams_legacy',
    'no_op_legacy',
    'ping_pong_streaming_legacy',
    'request_response_with_binary_metadata_and_payload_legacy',
    'request_response_with_metadata_and_payload_legacy',
    'request_response_with_payload_legacy',
    'request_response_with_trailing_metadata_and_payload_legacy',
    'request_with_large_metadata_legacy',
    'request_with_payload_legacy',
    'simple_delayed_request_legacy',
    'simple_request_legacy',
    'thread_stress_legacy',
    'writes_done_hangs_with_pending_read_legacy',
]


def main():
  json = {
      '#': 'generated with test/end2end/gen_build_json.py',
      'libs': [
          {
              'name': 'end2end_fixture_%s' % f,
              'build': 'private',
              'language': 'c',
              'secure': 'check',
              'src': ['test/core/end2end/fixtures/%s.c' % f]
          }
          for f in END2END_FIXTURES] + [
          {
              'name': 'end2end_test_%s' % t,
              'build': 'private',
              'language': 'c',
              'secure': 'no',
              'src': ['test/core/end2end/tests/%s.c' % t],
              'headers': ['test/core/end2end/tests/cancel_test_helpers.h']
          }
          for t in END2END_TESTS] + [
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
      for f in END2END_FIXTURES
      for t in END2END_TESTS]}
  print simplejson.dumps(json, sort_keys=True, indent=2 * ' ')


if __name__ == '__main__':
  main()
