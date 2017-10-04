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

_SERVER_HEALTH_CHECK_RECORD_NAME = 'dummy-health-check'
_SERVER_HEALTH_CHECK_RECORD_DATA = '123.123.123.123'

_TARGET_RECORDS_TO_SKIP_AGAINST_GCE = [
  # TODO: enable this once able to upload the very large TXT record
  # in this group to GCE DNS.
  'ipv4-config-causing-fallback-to-tcp',
]

def _append_zone_name(name, zone_name):
  return '%s.%s' % (name, zone_name)

def _build_expected_addrs_cmd_arg(expected_addrs):
  out = []
  for addr in expected_addrs:
    out.append('%s,%s' % (addr['address'], str(addr['is_balancer'])))
  return ';'.join(out)

def _data_for_type(r_type, r_data, common_zone_name):
  if r_type in ['A', 'AAAA']:
    return ','.join(r_data)
  if r_type == 'SRV':
    # This assert is valid only because there are not currently any
    # test cases in which an SRV query returns multiple records.
    assert len(r_data) == 1
    target = r_data[0].split(' ')[3]
    uploadable_target = '%s.%s' % (target, common_zone_name)
    uploadable = r_data[0].split(' ')
    uploadable[3] = uploadable_target
    return '%s' % ' '.join(uploadable)
  if r_type == 'TXT':
    assert len(r_data) == 1
    return r_data[0]

def _bind_convertable_json_form(test_cases, common_zone_name, records_to_skip):
  out = []
  for group in test_cases:
    if group['record_to_resolve'] in records_to_skip:
      continue
    for record_name in group['records'].keys():
      r_ttl = None
      all_r_data = {}
      for r_data in group['records'][record_name]:
        # enforce records have the same TTL only for simplicity
        if r_ttl is None:
          r_ttl = r_data['TTL']
        assert r_ttl == r_data['TTL'], '%s and %s differ' % (r_ttl, r_data['TTL'])
        r_type = r_data['type']
        if all_r_data.get(r_type) is None:
          all_r_data[r_type] = []
        all_r_data[r_type].append(r_data['data'])
      for r_type in all_r_data.keys():
        for r in out:
          assert r['name'] != record_name or r['type'] != r_type, 'attempt to add a duplicate record'
        out.append({
            'name': record_name,
            'ttl': r_ttl,
            'type': r_type,
            'data': _data_for_type(r_type, all_r_data[r_type], common_zone_name)
        })
  return out

def _auxiliary_records_for_local_dns_server():
  return [
      {
          'name': 'dummy-soa-record',
          'ttl': '0',
          'type': 'SOA',
          'data': 'dummy-master-server.com. dummy-admin-address.com 0 0 0 0 0',
      },
      {
          'name': _SERVER_HEALTH_CHECK_RECORD_NAME,
          'ttl': '0',
          'type': 'A',
          'data': _SERVER_HEALTH_CHECK_RECORD_DATA,
      },
  ]

def _gce_dns_zone_id(resolver_component_data):
  dns_name = resolver_component_data['resolver_tests_common_zone_name']
  return dns_name.replace('.', '-') + 'zone-id'

def _resolver_test_cases(resolver_component_data, records_to_skip):
  out = []
  for test_case in resolver_component_data['resolver_component_tests']:
    if test_case['record_to_resolve'] in records_to_skip:
      continue
    out.append({
      'target_name': _append_zone_name(test_case['record_to_resolve'],
                                       resolver_component_data['resolver_tests_common_zone_name']),
      'expected_addrs': _build_expected_addrs_cmd_arg(test_case['expected_addrs']),
      'expected_chosen_service_config': (test_case['expected_chosen_service_config'] or ''),
      'expected_lb_policy': (test_case['expected_lb_policy'] or ''),
    })
  return out

def main():
  resolver_component_data = ''
  with open('test/cpp/naming/resolver_test_record_groups.yaml') as f:
    resolver_component_data = yaml.load(f)
  all_integration_test_records = _bind_convertable_json_form(resolver_component_data['resolver_component_tests'],
                                                             resolver_component_data['resolver_tests_common_zone_name'], _TARGET_RECORDS_TO_SKIP_AGAINST_GCE)
  all_local_dns_server_test_records = _bind_convertable_json_form(resolver_component_data['resolver_component_tests'],
                                                             resolver_component_data['resolver_tests_common_zone_name'], [])
  all_local_dns_server_test_records += _auxiliary_records_for_local_dns_server()
  json_out = {
      'resolver_tests_common_zone_name': resolver_component_data['resolver_tests_common_zone_name'],
      'resolver_gce_integration_tests_zone_id': _gce_dns_zone_id(resolver_component_data),
      'all_integration_test_records': all_integration_test_records,
      'all_integration_test_records_json': json.dumps({'common_zone_name': resolver_component_data['resolver_tests_common_zone_name'],
                                                      'records': all_integration_test_records}, indent=4, sort_keys=True),
      'all_local_dns_server_test_records_json': json.dumps({'common_zone_name': resolver_component_data['resolver_tests_common_zone_name'],
                                                           'records': all_local_dns_server_test_records}, indent=4, sort_keys=True),
      'resolver_gce_integration_test_cases': _resolver_test_cases(resolver_component_data, _TARGET_RECORDS_TO_SKIP_AGAINST_GCE),
      'resolver_component_test_cases': _resolver_test_cases(resolver_component_data, []),
      'resolver_component_tests_health_check_record_name': '%s.%s' % (_SERVER_HEALTH_CHECK_RECORD_NAME, resolver_component_data['resolver_tests_common_zone_name']),
      'resolver_component_tests_health_check_record_data': _SERVER_HEALTH_CHECK_RECORD_DATA,
      'targets': [
          {
              'name': 'resolver_component_test' + unsecure_build_config_suffix,
              'build': 'test',
              'language': 'c++',
              'gtest': False,
              'run': False,
              'src': ['test/cpp/naming/resolver_component_test.cc'],
              'platforms': ['linux', 'posix', 'mac'],
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
      ]
  }

  print(yaml.dump(json_out))

if __name__ == '__main__':
  main()
