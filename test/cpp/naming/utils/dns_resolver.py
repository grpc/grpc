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

"""Makes DNS queries for A records to specified servers"""

import argparse
import signal
import twisted.internet.task as task
import twisted.names.client as client

def main():
  argp = argparse.ArgumentParser(description='Make DNS queries for A records')
  argp.add_argument('-s', '--server_host', default='127.0.0.1', type=str,
                    help='Host for DNS server to listen on for TCP and UDP.')
  argp.add_argument('-p', '--server_port', default=53, type=int,
                    help='Port that the DNS server is listening on.')
  argp.add_argument('-n', '--qname', default=None, type=str,
                    help=('Name of the record to query for. '))
  argp.add_argument('-t', '--timeout', default=1, type=int,
                    help=('Force process exit after this number of seconds.'))
  args = argp.parse_args()
  signal.alarm(args.timeout)
  def OnResolverResultAvailable(result):
    answers, authority, additional = result
    for a in answers:
      print(a.payload)
  def BeginQuery(reactor, qname):
    servers = [(args.server_host, args.server_port)]
    resolver = client.Resolver(servers=servers)
    deferred_result = resolver.lookupAddress(args.qname)
    deferred_result.addCallback(OnResolverResultAvailable)
    return deferred_result
  task.react(BeginQuery, [args.qname])

if __name__ == '__main__':
  main()
