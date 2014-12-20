#!/usr/bin/python

"""Generates the appropriate build.json data for all the end2end tests."""


import simplejson

END2END_FIXTURES = [
    'chttp2_fake_security',
    'chttp2_fullstack',
    'chttp2_simple_ssl_fullstack',
    'chttp2_simple_ssl_with_oauth2_fullstack',
    'chttp2_socket_pair',
    'chttp2_socket_pair_one_byte_at_a_time',
]


END2END_TESTS = [
    'cancel_after_accept',
    'cancel_after_accept_and_writes_closed',
    'cancel_after_invoke',
    'cancel_before_invoke',
    'cancel_in_a_vacuum',
    'disappearing_server',
    'early_server_shutdown_finishes_inflight_calls',
    'early_server_shutdown_finishes_tags',
    'invoke_large_request',
    'max_concurrent_streams',
    'no_op',
    'ping_pong_streaming',
    'request_response_with_binary_metadata_and_payload',
    'request_response_with_metadata_and_payload',
    'request_response_with_payload',
    'request_response_with_trailing_metadata_and_payload',
    'simple_delayed_request',
    'simple_request',
    'thread_stress',
    'writes_done_hangs_with_pending_read',
]


def main():
  json = {
      '#': 'generated with test/end2end/gen_build_json.py',
      'libs': [
          {
              'name': 'end2end_fixture_%s' % f,
              'build': 'private',
              'secure': True,
              'src': ['test/core/end2end/fixtures/%s.c' % f]
          }
          for f in END2END_FIXTURES] + [
          {
              'name': 'end2end_test_%s' % t,
              'build': 'private',
              'secure': False,
              'src': ['test/core/end2end/tests/%s.c' % t]
          }
          for t in END2END_TESTS] + [
          {
              'name': 'end2end_certs',
              'build': 'private',
              'src': [
                  "test/core/end2end/data/test_root_cert.c",
                  "test/core/end2end/data/prod_roots_certs.c",
                  "test/core/end2end/data/server1_cert.c",
                  "test/core/end2end/data/server1_key.c"
              ]
          }
          ],
      'targets': [
          {
              'name': '%s_%s_test' % (f, t),
              'build': 'test',
              'src': [],
              'deps': [
                  'end2end_fixture_%s' % f,
                  'end2end_test_%s' % t,
                  'end2end_certs',
                  'grpc_test_util',
                  'grpc',
                  'gpr'
              ]
          }
      for f in END2END_FIXTURES
      for t in END2END_TESTS]}
  print simplejson.dumps(json, sort_keys=True, indent=2 * ' ')


if __name__ == '__main__':
  main()

