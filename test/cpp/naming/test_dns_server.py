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

def start_local_dns_server(args):
  authority = twisted.names.authority.BindAuthority(args.zone_file_path)
  server = twisted.names.server.DNSServerFactory(
      authorities=[authority], verbose=2)
  server.noisy = 2
  twisted.internet.reactor.listenTCP(args.port, server)
  dns_proto = twisted.names.dns.DNSDatagramProtocol(server)
  dns_proto.noisy = 2
  twisted.internet.reactor.listenUDP(args.port, dns_proto)
  print('Starting local dns server on port %s' % args.port)
  print('Starting twisted.internet.reactor')
  sys.stdout.flush()
  twisted.internet.reactor.suggestThreadPoolSize(1)
  twisted.internet.reactor.run()

def _quit_on_signal(signum, _frame):
  print('Received SIGNAL %d. Quitting with exit code 0.' % signum)
  twisted.internet.reactor.stop()
  sys.stdout.flush()
  sys.exit(0)

def main():
  argp = argparse.ArgumentParser(description='Local DNS Server for resolver tests')
  argp.add_argument('-p', '--port', default=None, type=int,
                    help='Port for DNS server to listen on for TCP and UDP.')
  argp.add_argument('-z', '--zone_file_path', default=None, type=str,
                    help=('Path to bind formatted zone file containing records for this DNS server. '
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
