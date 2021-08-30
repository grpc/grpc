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
            0, (message_length >> 24) & 0xff, (message_length >> 16) & 0xff,
            (message_length >> 8) & 0xff, (message_length) & 0xff
        ] + send_message_length * [0]
        for frame_length in range(0, len(payload) + 1):
            is_end = frame_length == len(
                payload) and send_message_length == message_length
            frame = [(frame_length >> 16) & 0xff, (frame_length >> 8) & 0xff,
                     (frame_length) & 0xff, 0, 1 if is_end else 0, 0, 0, 0, 1
                    ] + payload[0:frame_length]
            text = esc_c(frame)
            if text not in done:
                print 'GRPC_RUN_BAD_CLIENT_TEST(verifier_%s, PFX_STR %s, %s);' % (
                    'succeeds' if is_end else 'fails', text,
                    '0' if is_end else 'GRPC_BAD_CLIENT_DISCONNECT')
                done.add(text)
