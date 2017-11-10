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


"""Generates the appropriate build.json data for all the bad_client tests."""


import collections
import yaml

TestOptions = collections.namedtuple('TestOptions', 'flaky cpu_cost')
default_test_options = TestOptions(False, 1.0)

# maps test names to options
BAD_CLIENT_TESTS = {
    'badreq': default_test_options,
    'connection_prefix': default_test_options._replace(cpu_cost=0.2),
    'headers': default_test_options._replace(cpu_cost=0.2),
    'initial_settings_frame': default_test_options._replace(cpu_cost=0.2),
    'head_of_line_blocking': default_test_options,
    # 'large_metadata': default_test_options, #disabling as per issue #11745
    'server_registered_method': default_test_options,
    'simple_request': default_test_options,
    'window_overflow': default_test_options,
    'unknown_frame': default_test_options,
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
              'test/core/bad_client/bad_client.cc'
            ],
            'headers': [
              'test/core/bad_client/bad_client.h'
            ],
            'vs_proj_dir': 'test/bad_client',
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
              'cpu_cost': BAD_CLIENT_TESTS[t].cpu_cost,
              'build': 'test',
              'language': 'c',
              'secure': 'no',
              'src': ['test/core/bad_client/tests/%s.cc' % t],
              'vs_proj_dir': 'test',
              'exclude_iomgrs': ['uv'],
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
