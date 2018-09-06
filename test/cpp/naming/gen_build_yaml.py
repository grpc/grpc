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


"""Generates the appropriate build.json data for all the naming tests."""


import yaml
import collections
import hashlib
import json

_LOCAL_DNS_SERVER_ADDRESS = '127.0.0.1:15353'

def _append_zone_name(name, zone_name):
  return '%s.%s' % (name, zone_name)

def _build_expected_addrs_cmd_arg(expected_addrs):
  out = []
  for addr in expected_addrs:
    out.append('%s,%s' % (addr['address'], str(addr['is_balancer'])))
  return ';'.join(out)

def _resolver_test_cases(resolver_component_data):
  out = []
  for test_case in resolver_component_data['resolver_component_tests']:
    target_name = _append_zone_name(
        test_case['record_to_resolve'],
        resolver_component_data['resolver_tests_common_zone_name'])
    out.append({
        'test_title': target_name,
        'arg_names_and_values': [
            ('target_name', target_name),
            ('expected_addrs',
             _build_expected_addrs_cmd_arg(test_case['expected_addrs'])),
            ('expected_chosen_service_config',
             (test_case['expected_chosen_service_config'] or '')),
            ('expected_lb_policy', (test_case['expected_lb_policy'] or '')),
        ],
    })
  return out

def main():
  resolver_component_data = ''
  with open('test/cpp/naming/resolver_test_record_groups.yaml') as f:
    resolver_component_data = yaml.load(f)

  json = {
      'resolver_tests_common_zone_name': resolver_component_data['resolver_tests_common_zone_name'],
      'resolver_component_test_cases': _resolver_test_cases(resolver_component_data),
      'targets': [
          {
              'name': 'resolver_component_test' + unsecure_build_config_suffix,
              'build': 'test',
              'language': 'c++',
              'gtest': False,
              'run': False,
              'src': ['test/cpp/naming/resolver_component_test.cc'],
              'platforms': ['linux', 'posix', 'mac', 'windows'],
              'deps': [
                  'grpc++_test_util' + unsecure_build_config_suffix,
                  'grpc_test_util' + unsecure_build_config_suffix,
                  'gpr_test_util',
                  'grpc++' + unsecure_build_config_suffix,
                  'grpc' + unsecure_build_config_suffix,
                  'gpr',
                  'grpc++_test_config',
              ],
          } for unsecure_build_config_suffix in ['_unsecure', '']
      ] + [
          {
              'name': 'resolver_component_tests_runner_invoker' + unsecure_build_config_suffix,
              'build': 'test',
              'language': 'c++',
              'gtest': False,
              'run': True,
              'src': ['test/cpp/naming/resolver_component_tests_runner_invoker.cc'],
              'platforms': ['linux', 'posix', 'mac'],
              'deps': [
                  'grpc++_test_util',
                  'grpc_test_util',
                  'gpr_test_util',
                  'grpc++',
                  'grpc',
                  'gpr',
                  'grpc++_test_config',
              ],
              'args': [
                  '--test_bin_name=resolver_component_test%s' % unsecure_build_config_suffix,
                  '--running_under_bazel=false',
              ],
          } for unsecure_build_config_suffix in ['_unsecure', '']
      ] + [
          {
              'name': 'address_sorting_test' + unsecure_build_config_suffix,
              'build': 'test',
              'language': 'c++',
              'gtest': True,
              'run': True,
              'src': ['test/cpp/naming/address_sorting_test.cc'],
              'platforms': ['linux', 'posix', 'mac', 'windows'],
              'deps': [
                  'grpc++_test_util' + unsecure_build_config_suffix,
                  'grpc_test_util' + unsecure_build_config_suffix,
                  'gpr_test_util',
                  'grpc++' + unsecure_build_config_suffix,
                  'grpc' + unsecure_build_config_suffix,
                  'gpr',
                  'grpc++_test_config',
              ],
          } for unsecure_build_config_suffix in ['_unsecure', '']
      ] + [
          {
          'name': 'cancel_ares_query_test',
          'build': 'test',
          'language': 'c++',
          'gtest': True,
          'run': True,
          'src': ['test/cpp/naming/cancel_ares_query_test.cc'],
          'platforms': ['linux', 'posix', 'mac', 'windows'],
          'deps': [
              'grpc++_test_util',
              'grpc_test_util',
              'gpr_test_util',
              'grpc++',
              'grpc',
              'gpr',
              'grpc++_test_config',
          ],
          },
      ]
  }

  print(yaml.dump(json))

if __name__ == '__main__':
  main()
