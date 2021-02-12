"""Tests for tap2pcap."""
from __future__ import print_function

import os
import subprocess as sp
import sys

import tap2pcap

# Validate that the tapped trace when run through tap2cap | tshark matches
# a golden output file for the tshark dump. Since we run tap2pcap in a
# subshell with a limited environment, the inferred time zone should be UTC.
if __name__ == '__main__':
  srcdir = os.path.join(os.getenv('TEST_SRCDIR'), 'envoy_api_canonical')
  tap_path = os.path.join(srcdir, 'tools/data/tap2pcap_h2_ipv4.pb_text')
  expected_path = os.path.join(srcdir, 'tools/data/tap2pcap_h2_ipv4.txt')
  pcap_path = os.path.join(os.getenv('TEST_TMPDIR'), 'generated.pcap')

  tap2pcap.Tap2Pcap(tap_path, pcap_path)
  actual_output = sp.check_output(['tshark', '-r', pcap_path, '-d', 'tcp.port==10000,http2', '-P'])
  with open(expected_path, 'rb') as f:
    expected_output = f.read()
  if actual_output != expected_output:
    print('Mismatch')
    print('Expected: %s' % expected_output)
    print('Actual: %s' % actual_output)
    sys.exit(1)
