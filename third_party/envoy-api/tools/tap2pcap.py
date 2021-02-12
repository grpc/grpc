"""Tool to convert Envoy tap trace format to PCAP.

Uses od and text2pcap (part of Wireshark) utilities to translate the Envoy
tap trace proto format to a PCAP file suitable for consuming in Wireshark
and other tools in the PCAP ecosystem. The TCP stream in the output PCAP is
synthesized based on the known IP/port/timestamps that Envoy produces in its
tap files; it is not a literal wire tap.

Usage:

bazel run @envoy_api_canonical//tools:tap2pcap <tap .pb/.pb_text> <pcap path>

Known issues:
- IPv6 PCAP generation has malformed TCP packets. This appears to be a text2pcap
issue.

TODO(htuch):
- Figure out IPv6 PCAP issue above, or file a bug once the root cause is clear.
"""
from __future__ import print_function

import datetime
import io
import socket
import subprocess as sp
import sys
import time

from google.protobuf import text_format

from envoy.data.tap.v2alpha import wrapper_pb2


def DumpEvent(direction, timestamp, data):
  dump = io.StringIO()
  dump.write('%s\n' % direction)
  # Adjust to local timezone
  adjusted_dt = timestamp.ToDatetime() - datetime.timedelta(seconds=time.altzone)
  dump.write('%s\n' % adjusted_dt)
  od = sp.Popen(['od', '-Ax', '-tx1', '-v'], stdout=sp.PIPE, stdin=sp.PIPE, stderr=sp.PIPE)
  packet_dump = od.communicate(data)[0]
  dump.write(packet_dump.decode())
  return dump.getvalue()


def Tap2Pcap(tap_path, pcap_path):
  wrapper = wrapper_pb2.TraceWrapper()
  if tap_path.endswith('.pb_text'):
    with open(tap_path, 'r') as f:
      text_format.Merge(f.read(), wrapper)
  else:
    with open(tap_path, 'r') as f:
      wrapper.ParseFromString(f.read())

  trace = wrapper.socket_buffered_trace
  local_address = trace.connection.local_address.socket_address.address
  local_port = trace.connection.local_address.socket_address.port_value
  remote_address = trace.connection.remote_address.socket_address.address
  remote_port = trace.connection.remote_address.socket_address.port_value

  dumps = []
  for event in trace.events:
    if event.HasField('read'):
      dumps.append(DumpEvent('I', event.timestamp, event.read.data.as_bytes))
    elif event.HasField('write'):
      dumps.append(DumpEvent('O', event.timestamp, event.write.data.as_bytes))

  ipv6 = False
  try:
    socket.inet_pton(socket.AF_INET6, local_address)
    ipv6 = True
  except socket.error:
    pass

  text2pcap_args = [
      'text2pcap', '-D', '-t', '%Y-%m-%d %H:%M:%S.', '-6' if ipv6 else '-4',
      '%s,%s' % (remote_address, local_address), '-T',
      '%d,%d' % (remote_port, local_port), '-', pcap_path
  ]
  text2pcap = sp.Popen(text2pcap_args, stdout=sp.PIPE, stdin=sp.PIPE)
  text2pcap.communicate('\n'.join(dumps).encode())


if __name__ == '__main__':
  if len(sys.argv) != 3:
    print('Usage: %s <tap .pb/.pb_text> <pcap path>' % sys.argv[0])
    sys.exit(1)
  Tap2Pcap(sys.argv[1], sys.argv[2])
