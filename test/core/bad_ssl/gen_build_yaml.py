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
