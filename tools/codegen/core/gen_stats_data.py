#!/usr/bin/env python2.7

# Copyright 2017 gRPC authors.
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
import ctypes
import math
import sys
import yaml

with open('src/core/lib/debug/stats_data.yaml') as f:
  attrs = yaml.load(f.read())

types = (
  (collections.namedtuple('Counter', 'name'), []),
  (collections.namedtuple('Histogram', 'name max buckets'), []),
)

inst_map = dict((t[0].__name__, t[1]) for t in types)

stats = []

for attr in attrs:
  found = False
  for t, lst in types:
    t_name = t.__name__.lower()
    if t_name in attr:
      name = attr[t_name]
      del attr[t_name]
      lst.append(t(name=name, **attr))
      found = True
      break
  assert found, "Bad decl: %s" % attr

def dbl2u64(d):
  return ctypes.c_ulonglong.from_buffer(ctypes.c_double(d)).value

def shift_works_until(mapped_bounds, shift_bits):
  for i, ab in enumerate(zip(mapped_bounds, mapped_bounds[1:])):
    a, b = ab
    if (a >> shift_bits) == (b >> shift_bits):
      return i
  return len(mapped_bounds)

def find_ideal_shift(mapped_bounds, max_size):
  best = None
  for shift_bits in reversed(range(0,64)):
    n = shift_works_until(mapped_bounds, shift_bits)
    if n == 0: continue
    table_size = mapped_bounds[n-1] >> shift_bits
    if table_size > max_size: continue
    if table_size > 65535: continue
    if best is None:
      best = (shift_bits, n, table_size)
    elif best[1] < n:
      best = (shift_bits, n, table_size)
  print best
  return best

def gen_map_table(mapped_bounds, shift_data):
  tbl = []
  cur = 0
  print mapped_bounds
  mapped_bounds = [x >> shift_data[0] for x in mapped_bounds]
  print mapped_bounds
  for i in range(0, mapped_bounds[shift_data[1]-1]):
    while i > mapped_bounds[cur]:
      cur += 1
    tbl.append(cur)
  return tbl

static_tables = []

def decl_static_table(values, type):
  global static_tables
  v = (type, values)
  for i, vp in enumerate(static_tables):
    if v == vp: return i
  print "ADD TABLE: %s %r" % (type, values)
  r = len(static_tables)
  static_tables.append(v)
  return r

def type_for_uint_table(table):
  mv = max(table)
  if mv < 2**8:
    return 'uint8_t'
  elif mv < 2**16:
    return 'uint16_t'
  elif mv < 2**32:
    return 'uint32_t'
  else:
    return 'uint64_t'

def gen_bucket_code(histogram):
  bounds = [0, 1]
  done_trivial = False
  done_unmapped = False
  first_nontrivial = None
  first_unmapped = None
  while len(bounds) < histogram.buckets:
    if len(bounds) == histogram.buckets - 1:
      nextb = int(histogram.max)
    else:
      mul = math.pow(float(histogram.max) / bounds[-1],
                     1.0 / (histogram.buckets - len(bounds)))
      nextb = int(math.ceil(bounds[-1] * mul))
    if nextb <= bounds[-1] + 1:
      nextb = bounds[-1] + 1
    elif not done_trivial:
      done_trivial = True
      first_nontrivial = len(bounds)
    bounds.append(nextb)
  bounds_idx = decl_static_table(bounds, 'int')
  if done_trivial:
    first_nontrivial_code = dbl2u64(first_nontrivial)
    code_bounds = [dbl2u64(x) - first_nontrivial_code for x in bounds]
    shift_data = find_ideal_shift(code_bounds[first_nontrivial:], 256 * histogram.buckets)
  #print first_nontrivial, shift_data, bounds
  #if shift_data is not None: print [hex(x >> shift_data[0]) for x in code_bounds[first_nontrivial:]]
  code = '  union { double dbl; uint64_t uint; } _val;\n'
  code += '_val.dbl = value;\n'
  code += 'if (_val.dbl < 0) _val.dbl = 0;\n'
  map_table = gen_map_table(code_bounds[first_nontrivial:], shift_data)
  if first_nontrivial is None:
    code += ('GRPC_STATS_INC_HISTOGRAM((exec_ctx), GRPC_STATS_HISTOGRAM_%s, (int)_val.dbl);\n'
             % histogram.name.upper())
  else:
    code += 'if (_val.dbl < %f) {\n' % first_nontrivial
    code += ('GRPC_STATS_INC_HISTOGRAM((exec_ctx), GRPC_STATS_HISTOGRAM_%s, (int)_val.dbl);\n'
             % histogram.name.upper())
    code += '} else {'
    first_nontrivial_code = dbl2u64(first_nontrivial)
    if shift_data is not None:
      map_table_idx = decl_static_table(map_table, type_for_uint_table(map_table))
      code += 'if (_val.uint < %dull) {\n' % ((map_table[-1] << shift_data[0]) + first_nontrivial_code)
      code += 'GRPC_STATS_INC_HISTOGRAM((exec_ctx), GRPC_STATS_HISTOGRAM_%s, ' % histogram.name.upper()
      code += 'grpc_stats_table_%d[((_val.uint - %dull) >> %d)] + %d);\n' % (map_table_idx, first_nontrivial_code, shift_data[0], first_nontrivial-1)
      code += '} else {\n'
    code += 'GRPC_STATS_INC_HISTOGRAM((exec_ctx), GRPC_STATS_HISTOGRAM_%s, '% histogram.name.upper()
    code += 'grpc_stats_histo_find_bucket_slow((exec_ctx), _val.dbl, grpc_stats_table_%d, %d));\n' % (bounds_idx, len(bounds))
    if shift_data is not None:
      code += '}'
    code += '}'
  return (code, bounds_idx)

# utility: print a big comment block into a set of files
def put_banner(files, banner):
  for f in files:
    print >>f, '/*'
    for line in banner:
      print >>f, ' * %s' % line
    print >>f, ' */'
    print >>f

with open('src/core/lib/debug/stats_data.h', 'w') as H:
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
    put_banner([H], [line[2:].rstrip() for line in copyright])

  put_banner([H], ["Automatically generated by tools/codegen/core/gen_stats_data.py"])

  print >>H, "#ifndef GRPC_CORE_LIB_DEBUG_STATS_DATA_H"
  print >>H, "#define GRPC_CORE_LIB_DEBUG_STATS_DATA_H"
  print >>H
  print >>H, "#include <inttypes.h>"
  print >>H, "#include \"src/core/lib/iomgr/exec_ctx.h\""
  print >>H

  for typename, instances in sorted(inst_map.items()):
    print >>H, "typedef enum {"
    for inst in instances:
      print >>H, "  GRPC_STATS_%s_%s," % (typename.upper(), inst.name.upper())
    print >>H, "  GRPC_STATS_%s_COUNT" % (typename.upper())
    print >>H, "} grpc_stats_%ss;" % (typename.lower())
    print >>H, "extern const char *grpc_stats_%s_name[GRPC_STATS_%s_COUNT];" % (
        typename.lower(), typename.upper())

  histo_start = []
  histo_buckets = []
  histo_bucket_boundaries = []

  print >>H, "typedef enum {"
  first_slot = 0
  for histogram in inst_map['Histogram']:
    histo_start.append(first_slot)
    histo_buckets.append(histogram.buckets)
    print >>H, "  GRPC_STATS_HISTOGRAM_%s_FIRST_SLOT = %d," % (histogram.name.upper(), first_slot)
    print >>H, "  GRPC_STATS_HISTOGRAM_%s_BUCKETS = %d," % (histogram.name.upper(), histogram.buckets)
    first_slot += histogram.buckets
  print >>H, "  GRPC_STATS_HISTOGRAM_BUCKETS = %d" % first_slot
  print >>H, "} grpc_stats_histogram_constants;"

  for ctr in inst_map['Counter']:
    print >>H, ("#define GRPC_STATS_INC_%s(exec_ctx) " +
                "GRPC_STATS_INC_COUNTER((exec_ctx), GRPC_STATS_COUNTER_%s)") % (
                ctr.name.upper(), ctr.name.upper())
  for histogram in inst_map['Histogram']:
    print >>H, "#define GRPC_STATS_INC_%s(exec_ctx, value) grpc_stats_inc_%s((exec_ctx), (int)(value))" % (
        histogram.name.upper(), histogram.name.lower())
    print >>H, "void grpc_stats_inc_%s(grpc_exec_ctx *exec_ctx, int x);" % histogram.name.lower()

  for i, tbl in enumerate(static_tables):
    print >>H, "extern const %s grpc_stats_table_%d[%d];" % (tbl[0], i, len(tbl[1]))

  print >>H, "extern const int grpc_stats_histo_buckets[%d];" % len(inst_map['Histogram'])
  print >>H, "extern const int grpc_stats_histo_start[%d];" % len(inst_map['Histogram'])
  print >>H, "extern const int *const grpc_stats_histo_bucket_boundaries[%d];" % len(inst_map['Histogram'])
  print >>H, "extern const double *const grpc_stats_histo_bucket_boundaries[%d];" % len(inst_map['Histogram'])
  print >>H, "extern void (*const grpc_stats_inc_histogram[%d])(grpc_exec_ctx *exec_ctx, int x);" % len(inst_map['Histogram'])

  print >>H
  print >>H, "#endif /* GRPC_CORE_LIB_DEBUG_STATS_DATA_H */"

with open('src/core/lib/debug/stats_data.c', 'w') as C:
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
    put_banner([C], [line[2:].rstrip() for line in copyright])

  put_banner([C], ["Automatically generated by tools/codegen/core/gen_stats_data.py"])

  print >>C, "#include \"src/core/lib/debug/stats_data.h\""
  print >>C, "#include \"src/core/lib/debug/stats.h\""
  print >>C, "#include \"src/core/lib/iomgr/exec_ctx.h\""

  histo_code = []
  for histogram in inst_map['Histogram']:
    code, bounds_idx = gen_bucket_code(histogram)
    histo_bucket_boundaries.append(bounds_idx)
    histo_code.append(code)

  for typename, instances in sorted(inst_map.items()):
    print >>C, "const char *grpc_stats_%s_name[GRPC_STATS_%s_COUNT] = {" % (
        typename.lower(), typename.upper())
    for inst in instances:
      print >>C, "  \"%s\"," % inst.name
    print >>C, "};"
  for i, tbl in enumerate(static_tables):
    print >>C, "const %s grpc_stats_table_%d[%d] = {%s};" % (
        tbl[0], i, len(tbl[1]), ','.join('%s' % x for x in tbl[1]))

  for histogram, code in zip(inst_map['Histogram'], histo_code):
    print >>C, ("void grpc_stats_inc_%s(grpc_exec_ctx *exec_ctx, double value) {%s}") % (
                histogram.name.lower(),
                code)

  print >>C, "const int grpc_stats_histo_buckets[%d] = {%s};" % (
      len(inst_map['Histogram']), ','.join('%s' % x for x in histo_buckets))
  print >>C, "const int grpc_stats_histo_start[%d] = {%s};" % (
      len(inst_map['Histogram']), ','.join('%s' % x for x in histo_start))
  print >>C, "const int *const grpc_stats_histo_bucket_boundaries[%d] = {%s};" % (
      len(inst_map['Histogram']), ','.join('grpc_stats_table_%d' % x for x in histo_bucket_boundaries))
  print >>C, "void (*const grpc_stats_inc_histogram[%d])(grpc_exec_ctx *exec_ctx, double x) = {%s};" % (
      len(inst_map['Histogram']), ','.join('grpc_stats_inc_%s' % histogram.name.lower() for histogram in inst_map['Histogram']))
