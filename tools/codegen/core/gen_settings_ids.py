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

import perfection

_SETTINGS = {
  'HEADER_TABLE_SIZE': 1,
  'ENABLE_PUSH': 2,
  'MAX_CONCURRENT_STREAMS': 3,
  'INITIAL_WINDOW_SIZE': 4,
  'MAX_FRAME_SIZE': 5,
  'MAX_HEADER_LIST_SIZE': 6,
  'GRPC_ALLOW_TRUE_BINARY_METADATA': 0xfe03,
}

p = perfection.hash_parameters(sorted(_SETTINGS.values()))
print p

def hash(i):
  i += p.offset
  x = i % p.t
  y = i / p.t
  return x + p.r[y]

print 'typedef enum {'
for name in sorted(_SETTINGS.keys()):
  index = _SETTINGS[name]
  print '  GRPC_CHTTP2_SETTING_%s = %d, /* wire id %d */' % (
      name, hash(index), index)
print '} grpc_chttp2_setting_id;'

print 'const uint16_t grpc_setting_id_to_wire_id[] = {%s};' % ','.join(
    '%d' % s for s in p.slots)

print """
bool grpc_wire_id_to_setting_id(uint32_t wire_id, grpc_chttp2_setting_id *out) {
  static const uint32_t r[] = {%(r)s};
  uint32_t i = wire_id %(offset_sign)s %(offset)d;
  uint32_t x = i %% %(t)d;
  uint32_t y = i / %(t)d;
  uint32_t h = x;
  if (y < GPR_ARRAY_SIZE(r)) {
    uint32_t delta = (uint32_t)r[y];
    h += delta;
  }
  *out = (grpc_chttp2_setting_id)i;
  return i < GPR_ARRAY_SIZE(grpc_setting_id_to_wire_id) && grpc_setting_id_to_wire_id[i] == wire_id;
}""" % {
      'r': ','.join('%d' % (r if r is not None else 0) for r in p.r),
      't': p.t,
      'offset': abs(p.offset),
      'offset_sign': '+' if p.offset > 0 else '-'
  }
