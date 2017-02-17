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

import hashlib
import itertools
import collections
import os
import sys
import subprocess
import re
import perfection

# configuration: a list of either strings or 2-tuples of strings
# a single string represents a static grpc_mdstr
# a 2-tuple represents a static grpc_mdelem (and appropriate grpc_mdstrs will
# also be created)

CONFIG = [
    # metadata strings
    'host',
    'grpc-timeout',
    'grpc-internal-encoding-request',
    'grpc-payload-bin',
    ':path',
    'grpc-encoding',
    'grpc-accept-encoding',
    'user-agent',
    ':authority',
    'grpc-message',
    'grpc-status',
    'grpc-tracing-bin',
    'grpc-stats-bin',
    '',
    # channel arg keys
    'grpc.wait_for_ready',
    'grpc.timeout',
    'grpc.max_request_message_bytes',
    'grpc.max_response_message_bytes',
    # well known method names
    '/grpc.lb.v1.LoadBalancer/BalanceLoad',
    # metadata elements
    ('grpc-status', '0'),
    ('grpc-status', '1'),
    ('grpc-status', '2'),
    ('grpc-encoding', 'identity'),
    ('grpc-encoding', 'gzip'),
    ('grpc-encoding', 'deflate'),
    ('te', 'trailers'),
    ('content-type', 'application/grpc'),
    (':method', 'POST'),
    (':status', '200'),
    (':status', '404'),
    (':scheme', 'http'),
    (':scheme', 'https'),
    (':scheme', 'grpc'),
    (':authority', ''),
    (':method', 'GET'),
    (':method', 'PUT'),
    (':path', '/'),
    (':path', '/index.html'),
    (':status', '204'),
    (':status', '206'),
    (':status', '304'),
    (':status', '400'),
    (':status', '500'),
    ('accept-charset', ''),
    ('accept-encoding', ''),
    ('accept-encoding', 'gzip, deflate'),
    ('accept-language', ''),
    ('accept-ranges', ''),
    ('accept', ''),
    ('access-control-allow-origin', ''),
    ('age', ''),
    ('allow', ''),
    ('authorization', ''),
    ('cache-control', ''),
    ('content-disposition', ''),
    ('content-encoding', ''),
    ('content-language', ''),
    ('content-length', ''),
    ('content-location', ''),
    ('content-range', ''),
    ('content-type', ''),
    ('cookie', ''),
    ('date', ''),
    ('etag', ''),
    ('expect', ''),
    ('expires', ''),
    ('from', ''),
    ('host', ''),
    ('if-match', ''),
    ('if-modified-since', ''),
    ('if-none-match', ''),
    ('if-range', ''),
    ('if-unmodified-since', ''),
    ('last-modified', ''),
    ('lb-token', ''),
    ('link', ''),
    ('location', ''),
    ('max-forwards', ''),
    ('proxy-authenticate', ''),
    ('proxy-authorization', ''),
    ('range', ''),
    ('referer', ''),
    ('refresh', ''),
    ('retry-after', ''),
    ('server', ''),
    ('set-cookie', ''),
    ('strict-transport-security', ''),
    ('transfer-encoding', ''),
    ('user-agent', ''),
    ('vary', ''),
    ('via', ''),
    ('www-authenticate', ''),
]

METADATA_BATCH_CALLOUTS = [
    ':path',
    ':method',
    ':status',
    ':authority',
    ':scheme',
    'te',
    'grpc-message',
    'grpc-status',
    'grpc-payload-bin',
    'grpc-encoding',
    'grpc-accept-encoding',
    'content-type',
    'grpc-internal-encoding-request',
    'user-agent',
    'host',
    'lb-token',
]

COMPRESSION_ALGORITHMS = [
    'identity',
    'deflate',
    'gzip',
]

# utility: mangle the name of a config
def mangle(elem, name=None):
  xl = {
      '-': '_',
      ':': '',
      '/': 'slash',
      '.': 'dot',
      ',': 'comma',
      ' ': '_',
  }
  def m0(x):
    if not x: return 'empty'
    r = ''
    for c in x:
      put = xl.get(c, c.lower())
      if not put: continue
      last_is_underscore = r[-1] == '_' if r else True
      if last_is_underscore and put == '_': continue
      elif len(put) > 1:
        if not last_is_underscore: r += '_'
        r += put
        r += '_'
      else:
        r += put
    if r[-1] == '_': r = r[:-1]
    return r
  def n(default, name=name):
    if name is None: return 'grpc_%s_' % default
    if name == '': return ''
    return 'grpc_%s_' % name
  if isinstance(elem, tuple):
    return '%s%s_%s' % (n('mdelem'), m0(elem[0]), m0(elem[1]))
  else:
    return '%s%s' % (n('mdstr'), m0(elem))

# utility: generate some hash value for a string
def fake_hash(elem):
  return hashlib.md5(elem).hexdigest()[0:8]

# utility: print a big comment block into a set of files
def put_banner(files, banner):
  for f in files:
    print >>f, '/*'
    for line in banner:
      print >>f, ' * %s' % line
    print >>f, ' */'
    print >>f

# build a list of all the strings we need
all_strs = list()
all_elems = list()
static_userdata = {}
# put metadata batch callouts first, to make the check of if a static metadata
# string is a callout trivial
for elem in METADATA_BATCH_CALLOUTS:
  if elem not in all_strs:
    all_strs.append(elem)
for elem in CONFIG:
  if isinstance(elem, tuple):
    if elem[0] not in all_strs:
      all_strs.append(elem[0])
    if elem[1] not in all_strs:
      all_strs.append(elem[1])
    if elem not in all_elems:
      all_elems.append(elem)
  else:
    if elem not in all_strs:
      all_strs.append(elem)
compression_elems = []
for mask in range(1, 1<<len(COMPRESSION_ALGORITHMS)):
  val = ','.join(COMPRESSION_ALGORITHMS[alg]
                 for alg in range(0, len(COMPRESSION_ALGORITHMS))
                 if (1 << alg) & mask)
  elem = ('grpc-accept-encoding', val)
  if val not in all_strs:
    all_strs.append(val)
  if elem not in all_elems:
    all_elems.append(elem)
  compression_elems.append(elem)
  static_userdata[elem] = 1 + (mask | 1)

# output configuration
args = sys.argv[1:]
H = None
C = None
D = None
if args:
  if 'header' in args:
    H = sys.stdout
  else:
    H = open('/dev/null', 'w')
  if 'source' in args:
    C = sys.stdout
  else:
    C = open('/dev/null', 'w')
  if 'dictionary' in args:
    D = sys.stdout
  else:
    D = open('/dev/null', 'w')
else:
  H = open(os.path.join(
      os.path.dirname(sys.argv[0]), '../../../src/core/lib/transport/static_metadata.h'), 'w')
  C = open(os.path.join(
      os.path.dirname(sys.argv[0]), '../../../src/core/lib/transport/static_metadata.c'), 'w')
  D = open(os.path.join(
      os.path.dirname(sys.argv[0]), '../../../test/core/end2end/fuzzers/hpack.dictionary'), 'w')

# copy-paste copyright notice from this file
with open(sys.argv[0]) as my_source:
  copyright = []
  for line in my_source:
    if line[0] != '#': break
  for line in my_source:
    if line[0] == '#':
      copyright.append(line)
      break
  for line in my_source:
    if line[0] != '#':
      break
    copyright.append(line)
  put_banner([H,C], [line[2:].rstrip() for line in copyright])


hex_bytes = [ord(c) for c in "abcdefABCDEF0123456789"]


def esc_dict(line):
  out = "\""
  for c in line:
    if 32 <= c < 127:
      if c != ord('"'):
        out += chr(c)
      else:
        out += "\\\""
    else:
      out += "\\x%02X" % c
  return out + "\""

put_banner([H,C],
"""WARNING: Auto-generated code.

To make changes to this file, change
tools/codegen/core/gen_static_metadata.py, and then re-run it.

See metadata.h for an explanation of the interface here, and metadata.c for
an explanation of what's going on.
""".splitlines())

print >>H, '#ifndef GRPC_CORE_LIB_TRANSPORT_STATIC_METADATA_H'
print >>H, '#define GRPC_CORE_LIB_TRANSPORT_STATIC_METADATA_H'
print >>H
print >>H, '#include "src/core/lib/transport/metadata.h"'
print >>H

print >>C, '#include "src/core/lib/transport/static_metadata.h"'
print >>C
print >>C, '#include "src/core/lib/slice/slice_internal.h"'
print >>C

str_ofs = 0
id2strofs = {}
for i, elem in enumerate(all_strs):
  id2strofs[i] = str_ofs
  str_ofs += len(elem);
def slice_def(i):
  return '{.refcount = &grpc_static_metadata_refcounts[%d], .data.refcounted = {g_bytes+%d, %d}}' % (i, id2strofs[i], len(all_strs[i]))

# validate configuration
for elem in METADATA_BATCH_CALLOUTS:
  assert elem in all_strs

print >>H, '#define GRPC_STATIC_MDSTR_COUNT %d' % len(all_strs)
print >>H, 'extern const grpc_slice grpc_static_slice_table[GRPC_STATIC_MDSTR_COUNT];'
for i, elem in enumerate(all_strs):
  print >>H, '/* "%s" */' % elem
  print >>H, '#define %s (grpc_static_slice_table[%d])' % (mangle(elem).upper(), i)
print >>H
print >>C, 'static uint8_t g_bytes[] = {%s};' % (','.join('%d' % ord(c) for c in ''.join(all_strs)))
print >>C
print >>C, 'static void static_ref(void *unused) {}'
print >>C, 'static void static_unref(grpc_exec_ctx *exec_ctx, void *unused) {}'
print >>C, 'static const grpc_slice_refcount_vtable static_sub_vtable = {static_ref, static_unref, grpc_slice_default_eq_impl, grpc_slice_default_hash_impl};';
print >>H, 'extern const grpc_slice_refcount_vtable grpc_static_metadata_vtable;';
print >>C, 'const grpc_slice_refcount_vtable grpc_static_metadata_vtable = {static_ref, static_unref, grpc_static_slice_eq, grpc_static_slice_hash};';
print >>C, 'static grpc_slice_refcount static_sub_refcnt = {&static_sub_vtable, &static_sub_refcnt};';
print >>H, 'extern grpc_slice_refcount grpc_static_metadata_refcounts[GRPC_STATIC_MDSTR_COUNT];'
print >>C, 'grpc_slice_refcount grpc_static_metadata_refcounts[GRPC_STATIC_MDSTR_COUNT] = {'
for i, elem in enumerate(all_strs):
  print >>C, '  {&grpc_static_metadata_vtable, &static_sub_refcnt},'
print >>C, '};'
print >>C
print >>H, '#define GRPC_IS_STATIC_METADATA_STRING(slice) \\'
print >>H, '  ((slice).refcount != NULL && (slice).refcount->vtable == &grpc_static_metadata_vtable)'
print >>H
print >>C, 'const grpc_slice grpc_static_slice_table[GRPC_STATIC_MDSTR_COUNT] = {'
for i, elem in enumerate(all_strs):
  print >>C, slice_def(i) + ','
print >>C, '};'
print >>C
print >>H, '#define GRPC_STATIC_METADATA_INDEX(static_slice) \\'
print >>H, '  ((int)((static_slice).refcount - grpc_static_metadata_refcounts))'
print >>H

print >>D, '# hpack fuzzing dictionary'
for i, elem in enumerate(all_strs):
  print >>D, '%s' % (esc_dict([len(elem)] + [ord(c) for c in elem]))
for i, elem in enumerate(all_elems):
  print >>D, '%s' % (esc_dict([0, len(elem[0])] + [ord(c) for c in elem[0]] +
                              [len(elem[1])] + [ord(c) for c in elem[1]]))

print >>H, '#define GRPC_STATIC_MDELEM_COUNT %d' % len(all_elems)
print >>H, 'extern grpc_mdelem_data grpc_static_mdelem_table[GRPC_STATIC_MDELEM_COUNT];'
print >>H, 'extern uintptr_t grpc_static_mdelem_user_data[GRPC_STATIC_MDELEM_COUNT];'
for i, elem in enumerate(all_elems):
  print >>H, '/* "%s": "%s" */' % elem
  print >>H, '#define %s (GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[%d], GRPC_MDELEM_STORAGE_STATIC))' % (mangle(elem).upper(), i)
print >>H
print >>C, 'uintptr_t grpc_static_mdelem_user_data[GRPC_STATIC_MDELEM_COUNT] = {'
print >>C, '  %s' % ','.join('%d' % static_userdata.get(elem, 0) for elem in all_elems)
print >>C, '};'
print >>C

def str_idx(s):
  for i, s2 in enumerate(all_strs):
    if s == s2:
      return i

def md_idx(m):
  for i, m2 in enumerate(all_elems):
    if m == m2:
      return i

def offset_trials(mink):
  yield 0
  for i in range(1, 100):
    for mul in [-1, 1]:
      yield mul * i

def perfect_hash(keys, name):
  p = perfection.hash_parameters(keys)
  def f(i, p=p):
    i += p.offset
    x = i % p.t
    y = i / p.t
    return x + p.r[y]
  return {
    'PHASHRANGE': p.t - 1 + max(p.r),
    'PHASHNKEYS': len(p.slots),
    'pyfunc': f,
    'code': """
static const int8_t %(name)s_r[] = {%(r)s};
static uint32_t %(name)s_phash(uint32_t i) {
  i %(offset_sign)s= %(offset)d;
  uint32_t x = i %% %(t)d;
  uint32_t y = i / %(t)d;
  uint32_t h = x;
  if (y < GPR_ARRAY_SIZE(%(name)s_r)) {
    uint32_t delta = (uint32_t)%(name)s_r[y];
    h += delta;
  }
  return h;
}
    """ % {
      'name': name,
      'r': ','.join('%d' % (r if r is not None else 0) for r in p.r),
      't': p.t,
      'offset': abs(p.offset),
      'offset_sign': '+' if p.offset > 0 else '-'
    }
  }


elem_keys = [str_idx(elem[0]) * len(all_strs) + str_idx(elem[1]) for elem in all_elems]
elem_hash = perfect_hash(elem_keys, "elems")
print >>C, elem_hash['code']

keys = [0] * int(elem_hash['PHASHRANGE'])
idxs = [255] * int(elem_hash['PHASHNKEYS'])
for i, k in enumerate(elem_keys):
    h = elem_hash['pyfunc'](k)
    assert keys[h] == 0
    keys[h] = k
    idxs[h] = i
print >>C, 'static const uint16_t elem_keys[] = {%s};' % ','.join('%d' % k for k in keys)
print >>C, 'static const uint8_t elem_idxs[] = {%s};' % ','.join('%d' % i for i in idxs)
print >>C

print >>H, 'grpc_mdelem grpc_static_mdelem_for_static_strings(int a, int b);'
print >>C, 'grpc_mdelem grpc_static_mdelem_for_static_strings(int a, int b) {'
print >>C, '  if (a == -1 || b == -1) return GRPC_MDNULL;'
print >>C, '  uint32_t k = (uint32_t)(a * %d + b);' % len(all_strs)
print >>C, '  uint32_t h = elems_phash(k);'
print >>C, '  return h < GPR_ARRAY_SIZE(elem_keys) && elem_keys[h] == k ? GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[elem_idxs[h]], GRPC_MDELEM_STORAGE_STATIC) : GRPC_MDNULL;'
print >>C, '}'
print >>C

print >>C, 'grpc_mdelem_data grpc_static_mdelem_table[GRPC_STATIC_MDELEM_COUNT] = {'
for a, b in all_elems:
  print >>C, '{%s,%s},' % (slice_def(str_idx(a)), slice_def(str_idx(b)))
print >>C, '};'

print >>H, 'typedef enum {'
for elem in METADATA_BATCH_CALLOUTS:
  print >>H, '  %s,' % mangle(elem, 'batch').upper()
print >>H, '  GRPC_BATCH_CALLOUTS_COUNT'
print >>H, '} grpc_metadata_batch_callouts_index;'
print >>H
print >>H, 'typedef union {'
print >>H, '  struct grpc_linked_mdelem *array[GRPC_BATCH_CALLOUTS_COUNT];'
print >>H, '  struct {'
for elem in METADATA_BATCH_CALLOUTS:
  print >>H, '  struct grpc_linked_mdelem *%s;' % mangle(elem, '').lower()
print >>H, '  } named;'
print >>H, '} grpc_metadata_batch_callouts;'
print >>H
print >>H, '#define GRPC_BATCH_INDEX_OF(slice) \\'
print >>H, '  (GRPC_IS_STATIC_METADATA_STRING((slice)) ? (grpc_metadata_batch_callouts_index)GPR_CLAMP(GRPC_STATIC_METADATA_INDEX((slice)), 0, GRPC_BATCH_CALLOUTS_COUNT) : GRPC_BATCH_CALLOUTS_COUNT)'
print >>H

print >>H, 'extern const uint8_t grpc_static_accept_encoding_metadata[%d];' % (1 << len(COMPRESSION_ALGORITHMS))
print >>C, 'const uint8_t grpc_static_accept_encoding_metadata[%d] = {' % (1 << len(COMPRESSION_ALGORITHMS))
print >>C, '0,%s' % ','.join('%d' % md_idx(elem) for elem in compression_elems)
print >>C, '};'
print >>C

print >>H, '#define GRPC_MDELEM_ACCEPT_ENCODING_FOR_ALGORITHMS(algs) (GRPC_MAKE_MDELEM(&grpc_static_mdelem_table[grpc_static_accept_encoding_metadata[(algs)]], GRPC_MDELEM_STORAGE_STATIC))'

print >>H, '#endif /* GRPC_CORE_LIB_TRANSPORT_STATIC_METADATA_H */'

H.close()
C.close()
