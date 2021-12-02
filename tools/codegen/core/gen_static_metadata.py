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

# Configuration: a list of either strings.
# A string represents a static grpc_mdstr.

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
    if elem not in all_strs:
        all_strs.append(elem)

# output configuration
args = sys.argv[1:]
STR_H = None
STR_C = None
if args:
    if 'str_header' in args:
        STR_H = sys.stdout
    else:
        STR_H = open('/dev/null', 'w')
    if 'str_source' in args:
        STR_C = sys.stdout
    else:
        STR_C = open('/dev/null', 'w')
else:
    STR_H = open(
        os.path.join(os.path.dirname(sys.argv[0]),
                     '../../../src/core/lib/slice/static_slice.h'), 'w')
    STR_C = open(
        os.path.join(os.path.dirname(sys.argv[0]),
                     '../../../src/core/lib/slice/static_slice.cc'), 'w')

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
    put_banner([STR_H, STR_C], [line[2:].rstrip() for line in copyright])

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


put_banner([STR_H, STR_C], """WARNING: Auto-generated code.

To make changes to this file, change
tools/codegen/core/gen_static_metadata.py, and then re-run it.

See metadata.h for an explanation of the interface here, and metadata.cc for
an explanation of what's going on.
""".splitlines())

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
static_mds = []
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

print("")
print('#endif /* GRPC_CORE_LIB_SLICE_STATIC_SLICE_H */', file=STR_H)

STR_H.close()
STR_C.close()
