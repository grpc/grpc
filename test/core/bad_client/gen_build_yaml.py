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


import collections
import yaml

TestOptions = collections.namedtuple('TestOptions', 'flaky')
default_test_options = TestOptions(False)

# maps test names to options
BAD_CLIENT_TESTS = {
    'connection_prefix': default_test_options,
    'initial_settings_frame': default_test_options,
}

def main():
  json = {
      '#': 'generated with test/bad_client/gen_build_json.py',
      'libs': [
          {
            'name': 'bad_client_test',
            'build': 'private',
            'language': 'c',
            'src': [
              'test/core/bad_client/bad_client.c'
            ],
            'headers': [
              'test/core/bad_client/bad_client.h'
            ],
            'vs_proj_dir': 'test',
            'deps': [
              'grpc_test_util_unsecure',
              'grpc_unsecure',
              'gpr_test_util',
              'gpr'
            ]
          }],
      'targets': [
          {
              'name': '%s_bad_client_test' % t,
              'build': 'test',
              'language': 'c',
              'secure': 'no',
              'src': ['test/core/bad_client/tests/%s.c' % t],
              'vs_proj_dir': 'test',
              'deps': [
                  'bad_client_test',
                  'grpc_test_util_unsecure',
                  'grpc_unsecure',
                  'gpr_test_util',
                  'gpr'
              ]
          }
      for t in sorted(BAD_CLIENT_TESTS.keys())]}
  print yaml.dump(json)


if __name__ == '__main__':
  main()
