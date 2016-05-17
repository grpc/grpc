#!/usr/bin/env python2.7

# Copyright 2015, Google Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

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

done = set()

for message_length in range(0, 3):
  for send_message_length in range(0, message_length + 1):
    payload = [
      0,
      (message_length >> 24) & 0xff,
      (message_length >> 16) & 0xff,
      (message_length >> 8) & 0xff,
      (message_length) & 0xff
    ] + send_message_length * [0]
    for frame_length in range(0, len(payload) + 1):
      is_end = frame_length == len(payload) and send_message_length == message_length
      frame = [
        (frame_length >> 16) & 0xff,
        (frame_length >> 8) & 0xff,
        (frame_length) & 0xff,
        0,
        1 if is_end else 0,
        0, 0, 0, 1
      ] + payload[0:frame_length]
      text = esc_c(frame)
      if text not in done:
        print 'GRPC_RUN_BAD_CLIENT_TEST(verifier_%s, PFX_STR %s, %s);' % (
            'succeeds' if is_end else 'fails', 
            text, 
            '0' if is_end else 'GRPC_BAD_CLIENT_DISCONNECT')
        done.add(text)
