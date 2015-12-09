#!/usr/bin/env python2.7
"""Read from stdin a set of colon separated http headers:
   :path: /foo/bar
   content-type: application/grpc
   Write a set of strings containing a hpack encoded http2 frame that
   represents said headers."""

import json
import sys

# parse input, fill in vals
vals = []
for line in sys.stdin:
  line = line.strip()
  if line == '': continue
  if line[0] == '#': continue
  key_tail, value = line[1:].split(':')
  key = (line[0] + key_tail).strip()
  value = value.strip()
  vals.append((key, value))

# generate frame payload binary data
payload_bytes = [[]] # reserve space for header
payload_len = 0
for key, value in vals:
  payload_line = []
  payload_line.append(0x10)
  assert(len(key) <= 126)
  payload_line.append(len(key))
  payload_line.extend(ord(c) for c in key)
  assert(len(value) <= 126)
  payload_line.append(len(value))
  payload_line.extend(ord(c) for c in value)
  payload_len += len(payload_line)
  payload_bytes.append(payload_line)

# fill in header
payload_bytes[0].extend([
    (payload_len >> 16) & 0xff,
    (payload_len >> 8) & 0xff,
    (payload_len) & 0xff,
    # header frame
    0x01,
    # flags
    0x04,
    # stream id
    0x00,
    0x00,
    0x00,
    0x01
])

hex_bytes = [ord(c) for c in "abcdefABCDEF0123456789"]

def esc_c(line):
  out = "\""
  last_was_hex = False
  for c in line:
    if 32 <= c < 127:
      if c in hex_bytes and last_was_hex:
        out += "\"\""
      if c != ord('"'):
        out += chr(c)
      else:
        out += "\\\""
      last_was_hex = False
    else:
      out += "\\x%02x" % c
      last_was_hex = True
  return out + "\""

# dump bytes
for line in payload_bytes:
  print esc_c(line)

