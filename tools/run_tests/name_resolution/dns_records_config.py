#!/usr/bin/env python
# Copyright 2017 gRPC authors.
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
#
# This script is invoked by a Jenkins pull request job and executes all
# args passed to this script if the pull request affect C/C++ code

# Source of truth for DNS records used in testing on GCE

ZONE_DNS = 'test.grpctestingexp.'
ZONE_NAME = 'exp-grpc-testing'
TTL = '2100'

SRV_PORT='1234'


class DnsRecord(object):
  def __init__(self, record_type, record_name, record_data):
    self.record_type = record_type
    self.record_name = record_name
    self.record_data = record_data
    self.record_class = 'IN'
    self.ttl = TTL

  def uploadable_data(self):
    return self.record_data.split(',')

def _create_records_for_testing():
  ipv4_single_target_dns = 'ipv4-single-target.%s' % ZONE_DNS
  ipv6_single_target_dns = 'ipv6-single-target.%s' % ZONE_DNS
  ipv4_multi_target_dns = 'ipv4-multi-target.%s' % ZONE_DNS
  ipv6_multi_target_dns = 'ipv6-multi-target.%s' % ZONE_DNS

  records = [
      DnsRecord('A', ipv4_single_target_dns, '1.2.3.4'),
      DnsRecord('A', ipv4_multi_target_dns, ','.join(['1.2.3.5',
                                                      '1.2.3.6',
                                                      '1.2.3.7'])),
      DnsRecord('AAAA', ipv6_single_target_dns, '2607:f8b0:400a:801::1001'),
      DnsRecord('AAAA', ipv6_multi_target_dns, ','.join(['2607:f8b0:400a:801::1002',
                                                         '2607:f8b0:400a:801::1003',
                                                         '2607:f8b0:400a:801::1004'])),
      DnsRecord('SRV', '_grpclb._tcp.srv-%s' % ipv4_single_target_dns, '0 0 %s %s' % (SRV_PORT, ipv4_single_target_dns)),
      DnsRecord('SRV', '_grpclb._tcp.srv-%s' % ipv4_multi_target_dns, '0 0 %s %s' % (SRV_PORT, ipv4_multi_target_dns)),
      DnsRecord('SRV', '_grpclb._tcp.srv-%s' % ipv6_single_target_dns, '0 0 %s %s' % (SRV_PORT, ipv6_single_target_dns)),
      DnsRecord('SRV', '_grpclb._tcp.srv-%s' % ipv6_multi_target_dns, '0 0 %s %s' % (SRV_PORT, ipv6_multi_target_dns)),
  ]
  return records

def srv_record_target_name(srv_record):
  # extract host from "priority weight port host" srv data
  return srv_record.record_data.split(' ')[3]

def srv_record_target_port(srv_record):
  # extract port from "priority weight port host" srv data
  return srv_record.record_data.split(' ')[2]

def a_record_target_ip(ip_record_name, dns_records):
  for r in dns_records:
    if r.record_name == ip_record_name:
      return r.record_data
  raise(Exception('no A/AAAA record found for target of srv record: %s' % srv_record.record_name))

def a_record_type(ip_record_name, dns_records):
  for r in dns_records:
    if r.record_name == ip_record_name:
      return r.record_type
  raise(Exception('no A/AAAA record found matching: %s' % ip_record_name))

def expected_result_for_a_record(a_record, expected_port=443):
  with_ports = []
  for ip in a_record.record_data.split(','):
    if a_record.record_type == 'A':
      with_ports.append('%s:%s' % (ip, expected_port))
    else:
      assert a_record.record_type == 'AAAA'
      with_ports.append('[%s]:%s' % (ip, expected_port))

  return ','.join(with_ports)

def expected_result_for_srv_record(srv_record, dns_records):
  srv_target_name = srv_record_target_name(srv_record)
  srv_target_port = srv_record_target_port(srv_record)

  for r in dns_records:
    if r.record_name == srv_target_name:
      return expected_result_for_a_record(r,
                                          expected_port=srv_target_port)
  raise Exception('target %s for srv record %s not found' %
    (srv_record_target_name, srv_record.record_name))

DNS_RECORDS = _create_records_for_testing()
