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
    return ' '.join(map(lambda x: '\"%s\"' % x, r_data))
  if r_type == 'SRV':
    assert len(r_data) == 1
    target = r_data[0].split(' ')[3]
    uploadable_target = '%s.%s' % (target, common_zone_name)
    uploadable = r_data[0].split(' ')
    uploadable[3] = uploadable_target
    return '\"%s\"' % ' '.join(uploadable)
  if r_type == 'TXT':
    assert len(r_data) == 1
    chunks = []
    all_data = r_data[0]
    cur = 0
    # Split TXT records that span more than 255 characters (the single
    # string length-limit in DNS) into multiple strings. Each string
    # needs to be wrapped with double-quotes, and all inner double-quotes
    # are escaped. The wrapping double-quotes and inner backslashes can be
    # counted towards the 255 character length limit (as observed with gcloud),
    # so make sure all strings fit within that limit.
    while len(all_data[cur:]) > 0:
      next_chunk = '\"'
      while len(next_chunk) < 254 and len(all_data[cur:]) > 0:
        if all_data[cur] == '\"':
          if len(next_chunk) < 253:
            next_chunk += '\\\"'
          else:
            break
        else:
          next_chunk += all_data[cur]
        cur += 1
      next_chunk += '\"'
      if len(next_chunk) > 255:
        raise Exception('Bug: next chunk is too long.')
      chunks.append(next_chunk)
    # Wrap the whole record in single quotes to make sure all strings
    # are associated with the same TXT record (to make it one bash token for
    # gcloud)
    return '\'%s\'' % ' '.join(chunks)

# Convert DNS records from their "within a test group" format
# of the yaml file to an easier form for the templates to use.
def _gcloud_uploadable_form(test_cases, common_zone_name):
  out = []
  for group in test_cases:
    if group['record_to_resolve'] in _TARGET_RECORDS_TO_SKIP_AGAINST_GCE:
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

  json = {
      'resolver_tests_common_zone_name': resolver_component_data['resolver_tests_common_zone_name'],
      'resolver_gce_integration_tests_zone_id': _gce_dns_zone_id(resolver_component_data),
      'all_integration_test_records': _gcloud_uploadable_form(resolver_component_data['resolver_component_tests'],
                                                              resolver_component_data['resolver_tests_common_zone_name']),
      'resolver_gce_integration_test_cases': _resolver_test_cases(resolver_component_data, _TARGET_RECORDS_TO_SKIP_AGAINST_GCE),
      'resolver_component_test_cases': _resolver_test_cases(resolver_component_data, []),
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
      ] + [
          {
              'name': 'address_sorting_test' + unsecure_build_config_suffix,
              'build': 'test',
              'language': 'c++',
              'gtest': True,
              'run': True,
              'src': ['test/cpp/naming/address_sorting_test.cc'],
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
      ]
  }

  print(yaml.dump(json))

if __name__ == '__main__':
  main()
