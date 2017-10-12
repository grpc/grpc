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

"""Starts a local DNS server for use in tests"""

import argparse
import sys
import yaml
import signal
import os

import twisted
import twisted.internet
import twisted.internet.reactor
import twisted.internet.threads
import twisted.internet.defer
import twisted.internet.protocol
import twisted.names
import twisted.names.client
import twisted.names.dns
import twisted.names.server
from twisted.names import client, server, common, authority, dns
import argparse

_SERVER_HEALTH_CHECK_RECORD_NAME = 'health-check-local-dns-server-is-alive.resolver-tests.grpctestingexp' # missing end '.' for twisted syntax
_SERVER_HEALTH_CHECK_RECORD_DATA = '123.123.123.123'

class NoFileAuthority(authority.FileAuthority):
  def __init__(self, soa, records):
    # skip FileAuthority
    common.ResolverBase.__init__(self)
    self.soa = soa
    self.records = records

def start_local_dns_server(args):
  all_records = {}
  def _push_record(name, r):
    print('pushing record: |%s|' % name)
    if all_records.get(name) is not None:
      all_records[name].append(r)
      return
    all_records[name] = [r]

  def _maybe_split_up_txt_data(name, txt_data, r_ttl):
    start = 0
    txt_data_list = []
    while len(txt_data[start:]) > 0:
      next_read = len(txt_data[start:])
      if next_read > 255:
        next_read = 255
      txt_data_list.append(txt_data[start:start+next_read])
      start += next_read
    _push_record(name, dns.Record_TXT(*txt_data_list, ttl=r_ttl))

  with open(args.records_config_path) as config:
    test_records_config = yaml.load(config)
  common_zone_name = test_records_config['resolver_tests_common_zone_name']
  for group in test_records_config['resolver_component_tests']:
    for name in group['records'].keys():
      for record in group['records'][name]:
        r_type = record['type']
        r_data = record['data']
        r_ttl = int(record['TTL'])
        record_full_name = '%s.%s' % (name, common_zone_name)
        assert record_full_name[-1] == '.'
        record_full_name = record_full_name[:-1]
        if r_type == 'A':
          _push_record(record_full_name, dns.Record_A(r_data, ttl=r_ttl))
        if r_type == 'AAAA':
          _push_record(record_full_name, dns.Record_AAAA(r_data, ttl=r_ttl))
        if r_type == 'SRV':
          p, w, port, target = r_data.split(' ')
          p = int(p)
          w = int(w)
          port = int(port)
          target_full_name = '%s.%s' % (target, common_zone_name)
          r_data = '%s %s %s %s' % (p, w, port, target_full_name)
          _push_record(record_full_name, dns.Record_SRV(p, w, port, target_full_name, ttl=r_ttl))
        if r_type == 'TXT':
          _maybe_split_up_txt_data(record_full_name, r_data, r_ttl)
  # Server health check record
  _push_record(_SERVER_HEALTH_CHECK_RECORD_NAME, dns.Record_A(_SERVER_HEALTH_CHECK_RECORD_DATA, ttl=0))
  soa_record = dns.Record_SOA(mname = common_zone_name)
  test_domain_com = NoFileAuthority(
    soa = (common_zone_name, soa_record),
    records = all_records,
  )
  server = twisted.names.server.DNSServerFactory(
      authorities=[test_domain_com], verbose=2)
  server.noisy = 2
  twisted.internet.reactor.listenTCP(args.port, server)
  dns_proto = twisted.names.dns.DNSDatagramProtocol(server)
  dns_proto.noisy = 2
  twisted.internet.reactor.listenUDP(args.port, dns_proto)
  print('starting local dns server on 127.0.0.1:%s' % args.port)
  print('starting twisted.internet.reactor')
  twisted.internet.reactor.suggestThreadPoolSize(1)
  twisted.internet.reactor.run()

def _quit_on_signal(signum, _frame):
  print('Received SIGNAL %d. Quitting with exit code 0' % signum)
  twisted.internet.reactor.stop()
  sys.stdout.flush()
  sys.exit(0)

def main():
  argp = argparse.ArgumentParser(description='Local DNS Server for resolver tests')
  argp.add_argument('-p', '--port', default=None, type=int,
                    help='Port for DNS server to listen on for TCP and UDP.')
  argp.add_argument('-r', '--records_config_path', default=None, type=str,
                    help=('Directory of resolver_test_record_groups.yaml file. '
                          'Defauls to path needed when the test is invoked as part of run_tests.py.'))
  args = argp.parse_args()
  signal.signal(signal.SIGALRM, _quit_on_signal)
  signal.signal(signal.SIGTERM, _quit_on_signal)
  signal.signal(signal.SIGINT, _quit_on_signal)
  # Prevent zombies. Tests that use this server are short-lived.
  signal.alarm(2 * 60)
  start_local_dns_server(args)

if __name__ == '__main__':
  main()
