#!/usr/bin/env python3

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

import collections
import hashlib
import itertools
import os
import re
import subprocess
import sys

import perfection

# Configuration: a list of either strings or 2-tuples of strings.
# A single string represents a static grpc_mdstr.
# A 2-tuple represents a static grpc_mdelem (and appropriate grpc_mdstrs will
# also be created).
# The list of 2-tuples must begin with the static hpack table elements as
# defined by RFC 7541 and be in the same order because of an hpack encoding
# performance optimization that relies on this. If you want to change this, then
# you must change the implementation of the encoding optimization as well.

CONFIG = [
    # metadata strings
    'grpc-timeout',
    '',
    # well known method names
    '/grpc.lb.v1.LoadBalancer/BalanceLoad',
    '/envoy.service.load_stats.v2.LoadReportingService/StreamLoadStats',
    '/envoy.service.load_stats.v3.LoadReportingService/StreamLoadStats',
    '/grpc.health.v1.Health/Watch',
    '/envoy.service.discovery.v2.AggregatedDiscoveryService/StreamAggregatedResources',
    '/envoy.service.discovery.v3.AggregatedDiscoveryService/StreamAggregatedResources',
    # te: trailers strings
    'te',
    'trailers',
    # metadata elements
    # begin hpack static elements
    (':authority', ''),
    (':method', 'GET'),
    (':method', 'POST'),
    (':path', '/'),
    (':path', '/index.html'),
    (':scheme', 'http'),
    (':scheme', 'https'),
    (':status', '200'),
    (':status', '204'),
    (':status', '206'),
    (':status', '304'),
    (':status', '400'),
    (':status', '404'),
    (':status', '500'),
    ('accept-charset', ''),
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
    # end hpack static elements
    ('lb-cost-bin', ''),
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
        if not x:
            return 'empty'
        r = ''
        for c in x:
            put = xl.get(c, c.lower())
            if not put:
                continue
            last_is_underscore = r[-1] == '_' if r else True
            if last_is_underscore and put == '_':
                continue
            elif len(put) > 1:
                if not last_is_underscore:
                    r += '_'
                r += put
                r += '_'
            else:
                r += put
        if r[-1] == '_':
            r = r[:-1]
        return r

    def n(default, name=name):
        if name is None:
            return 'grpc_%s_' % default
        if name == '':
            return ''
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
        print('/*', file=f)
        for line in banner:
            print(' * %s' % line, file=f)
        print(' */', file=f)
        print('', file=f)


# build a list of all the strings we need
all_strs = list()
all_elems = list()
static_userdata = {}
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

# output configuration
args = sys.argv[1:]
MD_H = None
MD_C = None
STR_H = None
STR_C = None
D = None
if args:
    if 'md_header' in args:
        MD_H = sys.stdout
    else:
        MD_H = open('/dev/null', 'w')
    if 'md_source' in args:
        MD_C = sys.stdout
    else:
        MD_C = open('/dev/null', 'w')
    if 'str_header' in args:
        STR_H = sys.stdout
    else:
        STR_H = open('/dev/null', 'w')
    if 'str_source' in args:
        STR_C = sys.stdout
    else:
        STR_C = open('/dev/null', 'w')
    if 'dictionary' in args:
        D = sys.stdout
    else:
        D = open('/dev/null', 'w')
else:
    MD_H = open(
        os.path.join(os.path.dirname(sys.argv[0]),
                     '../../../src/core/lib/transport/static_metadata.h'), 'w')
    MD_C = open(
        os.path.join(os.path.dirname(sys.argv[0]),
                     '../../../src/core/lib/transport/static_metadata.cc'), 'w')
    STR_H = open(
        os.path.join(os.path.dirname(sys.argv[0]),
                     '../../../src/core/lib/slice/static_slice.h'), 'w')
    STR_C = open(
        os.path.join(os.path.dirname(sys.argv[0]),
                     '../../../src/core/lib/slice/static_slice.cc'), 'w')
    D = open(
        os.path.join(os.path.dirname(sys.argv[0]),
                     '../../../test/core/end2end/fuzzers/hpack.dictionary'),
        'w')

# copy-paste copyright notice from this file
with open(sys.argv[0]) as my_source:
    copyright = []
    for line in my_source:
        if line[0] != '#':
            break
    for line in my_source:
        if line[0] == '#':
            copyright.append(line)
            break
    for line in my_source:
        if line[0] != '#':
            break
        copyright.append(line)
    put_banner([MD_H, MD_C, STR_H, STR_C],
               [line[2:].rstrip() for line in copyright])

hex_bytes = [ord(c) for c in 'abcdefABCDEF0123456789']


def esc_dict(line):
    out = "\""
    for c in line:
        if 32 <= c < 127:
            if c != ord('"'):
                out += chr(c)
            else:
                out += "\\\""
        else:
            out += '\\x%02X' % c
    return out + "\""


put_banner([MD_H, MD_C, STR_H, STR_C], """WARNING: Auto-generated code.

To make changes to this file, change
tools/codegen/core/gen_static_metadata.py, and then re-run it.

See metadata.h for an explanation of the interface here, and metadata.cc for
an explanation of what's going on.
""".splitlines())

print('#ifndef GRPC_CORE_LIB_TRANSPORT_STATIC_METADATA_H', file=MD_H)
print('#define GRPC_CORE_LIB_TRANSPORT_STATIC_METADATA_H', file=MD_H)
print('', file=MD_H)
print('#include <grpc/support/port_platform.h>', file=MD_H)
print('', file=MD_H)
print('#include <cstdint>', file=MD_H)
print('', file=MD_H)
print('#include "src/core/lib/transport/metadata.h"', file=MD_H)
print('#include "src/core/lib/slice/static_slice.h"', file=MD_H)
print('', file=MD_H)
print('#ifndef GRPC_CORE_LIB_SLICE_STATIC_SLICE_H', file=STR_H)
print('#define GRPC_CORE_LIB_SLICE_STATIC_SLICE_H', file=STR_H)
print('', file=STR_H)
print('#include <grpc/support/port_platform.h>', file=STR_H)
print('', file=STR_H)
print('#include <cstdint>', file=STR_H)
print('#include <type_traits>', file=STR_H)
print('#include "src/core/lib/slice/slice_utils.h"', file=STR_H)
print('#include "src/core/lib/slice/slice_refcount_base.h"', file=STR_H)
print('', file=STR_H)
print('#include <grpc/support/port_platform.h>', file=MD_C)
print('', file=MD_C)
print('#include "src/core/lib/transport/static_metadata.h"', file=MD_C)
print('', file=MD_C)
print('#include "src/core/lib/slice/slice_internal.h"', file=MD_C)
print('', file=MD_C)
print('#include <grpc/support/port_platform.h>', file=STR_C)
print('', file=STR_C)
print('#include "src/core/lib/slice/static_slice.h"', file=STR_C)
print('', file=STR_C)

str_ofs = 0
id2strofs = {}
for i, elem in enumerate(all_strs):
    id2strofs[i] = str_ofs
    str_ofs += len(elem)


def slice_def_for_ctx(i):
    return (
        'StaticMetadataSlice(&g_static_metadata_slice_refcounts[%d].base, %d, g_static_metadata_bytes+%d)'
    ) % (i, len(all_strs[i]), id2strofs[i])


def slice_def(i):
    return (
        'StaticMetadataSlice(&g_static_metadata_slice_refcounts[%d].base, %d, g_static_metadata_bytes+%d)'
    ) % (i, len(all_strs[i]), id2strofs[i])


def str_idx(s):
    for i, s2 in enumerate(all_strs):
        if s == s2:
            return i


static_slice_dest_assert = (
    'static_assert(std::is_trivially_destructible' +
    '<grpc_core::StaticMetadataSlice>::value, '
    '"StaticMetadataSlice must be trivially destructible.");')
print(static_slice_dest_assert, file=STR_H)
print('#define GRPC_STATIC_MDSTR_COUNT %d' % len(all_strs), file=STR_H)
for i, elem in enumerate(all_strs):
    print('/* "%s" */' % elem, file=STR_H)
    print('#define %s (::grpc_core::g_static_metadata_slice_table[%d])' %
          (mangle(elem).upper(), i),
          file=STR_H)
print('', file=STR_H)
print('namespace grpc_core {', file=STR_C)
print('',
      'const uint8_t g_static_metadata_bytes[] = {%s};' %
      (','.join('%d' % ord(c) for c in ''.join(all_strs))),
      file=STR_C)
print('', file=STR_C)
print('''
namespace grpc_core {
extern StaticSliceRefcount g_static_metadata_slice_refcounts[GRPC_STATIC_MDSTR_COUNT];
extern const StaticMetadataSlice g_static_metadata_slice_table[GRPC_STATIC_MDSTR_COUNT];
extern const uint8_t g_static_metadata_bytes[];
}
''',
      file=STR_H)
print('grpc_slice_refcount StaticSliceRefcount::kStaticSubRefcount;',
      file=STR_C)
print('''
StaticSliceRefcount
    g_static_metadata_slice_refcounts[GRPC_STATIC_MDSTR_COUNT] = {
''',
      file=STR_C)
for i, elem in enumerate(all_strs):
    print('  StaticSliceRefcount(%d), ' % i, file=STR_C)
print('};', file=STR_C)  # static slice refcounts
print('', file=STR_C)
print('''
  const StaticMetadataSlice
    g_static_metadata_slice_table[GRPC_STATIC_MDSTR_COUNT] = {
''',
      file=STR_C)
for i, elem in enumerate(all_strs):
    print(slice_def_for_ctx(i) + ',', file=STR_C)
print('};', file=STR_C)  # static slices
print('namespace grpc_core {', file=MD_C)
print('StaticMetadata g_static_mdelem_table[GRPC_STATIC_MDELEM_COUNT] = {',
      file=MD_C)
for idx, (a, b) in enumerate(all_elems):
    print('StaticMetadata(%s,%s, %d),' %
          (slice_def_for_ctx(str_idx(a)), slice_def_for_ctx(str_idx(b)), idx),
          file=MD_C)
print('};', file=MD_C)  # static_mdelem_table
print(('''
/* Warning: the core static metadata currently operates under the soft constraint
that the first GRPC_CHTTP2_LAST_STATIC_ENTRY (61) entries must contain
metadata specified by the http2 hpack standard. The CHTTP2 transport reads the
core metadata with this assumption in mind. If the order of the core static
metadata is to be changed, then the CHTTP2 transport must be changed as well to
stop relying on the core metadata. */
'''),
      file=MD_C)
print(('grpc_mdelem '
       'g_static_mdelem_manifested[GRPC_STATIC_MDELEM_COUNT] = {'),
      file=MD_C)
print('// clang-format off', file=MD_C)
static_mds = []
for i, elem in enumerate(all_elems):
    md_name = mangle(elem).upper()
    md_human_readable = '"%s": "%s"' % elem
    md_spec = '    /* %s: \n     %s */\n' % (md_name, md_human_readable)
    md_spec += '    GRPC_MAKE_MDELEM(\n'
    md_spec += (('        &g_static_mdelem_table[%d].data(),\n' % i) +
                '        GRPC_MDELEM_STORAGE_STATIC)')
    static_mds.append(md_spec)
print(',\n'.join(static_mds), file=MD_C)
print('// clang-format on', file=MD_C)
print(('};'), file=MD_C)  # static_mdelem_manifested
print('}', file=MD_C)  # namespace grpc_core
print('}', file=STR_C)  # namespace grpc_core

print('', file=MD_C)
print('#define GRPC_IS_STATIC_METADATA_STRING(slice) \\', file=STR_H)
print(('  ((slice).refcount != NULL && (slice).refcount->GetType() == '
       'grpc_slice_refcount::Type::STATIC)'),
      file=STR_H)
print('', file=STR_H)
print('', file=STR_C)
print('#define GRPC_STATIC_METADATA_INDEX(static_slice) \\', file=STR_H)
print(
    '(reinterpret_cast<::grpc_core::StaticSliceRefcount*>((static_slice).refcount)->index)',
    file=STR_H)
print('', file=STR_H)

print('# hpack fuzzing dictionary', file=D)
for i, elem in enumerate(all_strs):
    print('%s' % (esc_dict([len(elem)] + [ord(c) for c in elem])), file=D)
for i, elem in enumerate(all_elems):
    print('%s' % (esc_dict([0, len(elem[0])] + [ord(c) for c in elem[0]] +
                           [len(elem[1])] + [ord(c) for c in elem[1]])),
          file=D)

print('#define GRPC_STATIC_MDELEM_COUNT %d' % len(all_elems), file=MD_H)
print('''
namespace grpc_core {
extern StaticMetadata g_static_mdelem_table[GRPC_STATIC_MDELEM_COUNT];
extern grpc_mdelem g_static_mdelem_manifested[GRPC_STATIC_MDELEM_COUNT];
}
''',
      file=MD_H)
print(('extern uintptr_t '
       'grpc_static_mdelem_user_data[GRPC_STATIC_MDELEM_COUNT];'),
      file=MD_H)

for i, elem in enumerate(all_elems):
    md_name = mangle(elem).upper()
    print('/* "%s": "%s" */' % elem, file=MD_H)
    print(('#define %s (::grpc_core::g_static_mdelem_manifested[%d])' %
           (md_name, i)),
          file=MD_H)
print('', file=MD_H)

print(('uintptr_t grpc_static_mdelem_user_data[GRPC_STATIC_MDELEM_COUNT] '
       '= {'),
      file=MD_C)
print('  %s' %
      ','.join('%d' % static_userdata.get(elem, 0) for elem in all_elems),
      file=MD_C)
print('};', file=MD_C)
print('', file=MD_C)


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
        y = i // p.t
        return x + p.r[y]

    return {
        'PHASHNKEYS':
            len(p.slots),
        'pyfunc':
            f,
        'code':
            """
static const int8_t %(name)s_r[] = {%(r)s};
static uint32_t %(name)s_phash(uint32_t i) {
  i %(offset_sign)s= %(offset)d;
  uint32_t x = i %% %(t)d;
  uint32_t y = i / %(t)d;
  uint32_t h = x;
  if (y < GPR_ARRAY_SIZE(%(name)s_r)) {
    uint32_t delta = static_cast<uint32_t>(%(name)s_r[y]);
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


elem_keys = [
    str_idx(elem[0]) * len(all_strs) + str_idx(elem[1]) for elem in all_elems
]
elem_hash = perfect_hash(elem_keys, 'elems')
print(elem_hash['code'], file=MD_C)

keys = [0] * int(elem_hash['PHASHNKEYS'])
idxs = [255] * int(elem_hash['PHASHNKEYS'])
for i, k in enumerate(elem_keys):
    h = elem_hash['pyfunc'](k)
    assert keys[h] == 0
    keys[h] = k
    idxs[h] = i
print('static const uint16_t elem_keys[] = {%s};' %
      ','.join('%d' % k for k in keys),
      file=MD_C)
print('static const uint8_t elem_idxs[] = {%s};' %
      ','.join('%d' % i for i in idxs),
      file=MD_C)
print('', file=MD_C)

print(
    'grpc_mdelem grpc_static_mdelem_for_static_strings(intptr_t a, intptr_t b);',
    file=MD_H)
print(
    'grpc_mdelem grpc_static_mdelem_for_static_strings(intptr_t a, intptr_t b) {',
    file=MD_C)
print('  if (a == -1 || b == -1) return GRPC_MDNULL;', file=MD_C)
print('  uint32_t k = static_cast<uint32_t>(a * %d + b);' % len(all_strs),
      file=MD_C)
print('  uint32_t h = elems_phash(k);', file=MD_C)
print(
    '  return h < GPR_ARRAY_SIZE(elem_keys) && elem_keys[h] == k && elem_idxs[h] != 255 ? GRPC_MAKE_MDELEM(&grpc_core::g_static_mdelem_table[elem_idxs[h]].data(), GRPC_MDELEM_STORAGE_STATIC) : GRPC_MDNULL;',
    file=MD_C)
print('}', file=MD_C)
print('', file=MD_C)

print('#endif /* GRPC_CORE_LIB_TRANSPORT_STATIC_METADATA_H */', file=MD_H)
print('#endif /* GRPC_CORE_LIB_SLICE_STATIC_SLICE_H */', file=STR_H)

MD_H.close()
MD_C.close()
STR_H.close()
STR_C.close()
