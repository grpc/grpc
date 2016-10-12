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

TestOptions = collections.namedtuple('TestOptions', 'flaky cpu_cost')
default_test_options = TestOptions(False, 1.0)

# maps test names to options
BAD_CLIENT_TESTS = {
    'cert': default_test_options._replace(cpu_cost=0.1),
    # Disabling this test because it does not link correctly as written
    # 'alpn': default_test_options._replace(cpu_cost=0.1),
}

def main():
  json = {
      '#': 'generated with test/bad_ssl/gen_build_json.py',
      'libs': [
          {
              'name': 'bad_ssl_test_server',
              'build': 'private',
              'language': 'c',
              'src': ['test/core/bad_ssl/server_common.c'],
              'headers': ['test/core/bad_ssl/server_common.h'],
              'vs_proj_dir': 'test',
              'platforms': ['linux', 'posix', 'mac'],
              'deps': [
                  'grpc_test_util',
                  'grpc',
                  'gpr_test_util',
                  'gpr'
              ]
          }
      ],
      'targets': [
          {
              'name': 'bad_ssl_%s_server' % t,
              'build': 'test',
              'language': 'c',
              'run': False,
              'src': ['test/core/bad_ssl/servers/%s.c' % t],
              'vs_proj_dir': 'test/bad_ssl',
              'platforms': ['linux', 'posix', 'mac'],
              'deps': [
                  'bad_ssl_test_server',
                  'grpc_test_util',
                  'grpc',
                  'gpr_test_util',
                  'gpr'
              ]
          }
      for t in sorted(BAD_CLIENT_TESTS.keys())] + [
          {
              'name': 'bad_ssl_%s_test' % t,
              'cpu_cost': BAD_CLIENT_TESTS[t].cpu_cost,
              'build': 'test',
              'language': 'c',
              'src': ['test/core/bad_ssl/bad_ssl_test.c'],
              'vs_proj_dir': 'test',
              'platforms': ['linux', 'posix', 'mac'],
              'deps': [
                  'grpc_test_util',
                  'grpc',
                  'gpr_test_util',
                  'gpr'
              ]
          }
      for t in sorted(BAD_CLIENT_TESTS.keys())]}
  print yaml.dump(json)


if __name__ == '__main__':
  main()
