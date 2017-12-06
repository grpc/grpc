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
import json

with open('src/core/lib/debug/stats_data.yaml') as f:
  attrs = yaml.load(f.read())

REQUIRED_FIELDS = ['name', 'doc']

def make_type(name, fields):
  return (collections.namedtuple(name, ' '.join(list(set(REQUIRED_FIELDS + fields)))), [])

def c_str(s, encoding='ascii'):
   if isinstance(s, unicode):
      s = s.encode(encoding)
   result = ''
   for c in s:
      if not (32 <= ord(c) < 127) or c in ('\\', '"'):
         result += '\\%03o' % ord(c)
      else:
         result += c
   return '"' + result + '"'

types = (
  make_type('Counter', []),
  make_type('Histogram', ['max', 'buckets']),
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
  while len(bounds) < histogram.buckets + 1:
    if len(bounds) == histogram.buckets:
      nextb = int(histogram.max)
    else:
      mul = math.pow(float(histogram.max) / bounds[-1],
                     1.0 / (histogram.buckets + 1 - len(bounds)))
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
  code = 'value = GPR_CLAMP(value, 0, %d);\n' % histogram.max
  map_table = gen_map_table(code_bounds[first_nontrivial:], shift_data)
  if first_nontrivial is None:
    code += ('GRPC_STATS_INC_HISTOGRAM((exec_ctx), GRPC_STATS_HISTOGRAM_%s, value);\n'
             % histogram.name.upper())
  else:
    code += 'if (value < %d) {\n' % first_nontrivial
    code += ('GRPC_STATS_INC_HISTOGRAM((exec_ctx), GRPC_STATS_HISTOGRAM_%s, value);\n'
             % histogram.name.upper())
    code += 'return;\n'
    code += '}'
    first_nontrivial_code = dbl2u64(first_nontrivial)
    if shift_data is not None:
      map_table_idx = decl_static_table(map_table, type_for_uint_table(map_table))
      code += 'union { double dbl; uint64_t uint; } _val, _bkt;\n'
      code += '_val.dbl = value;\n'
      code += 'if (_val.uint < %dull) {\n' % ((map_table[-1] << shift_data[0]) + first_nontrivial_code)
      code += 'int bucket = '
      code += 'grpc_stats_table_%d[((_val.uint - %dull) >> %d)] + %d;\n' % (map_table_idx, first_nontrivial_code, shift_data[0], first_nontrivial)
      code += '_bkt.dbl = grpc_stats_table_%d[bucket];\n' % bounds_idx
      code += 'bucket -= (_val.uint < _bkt.uint);\n'
      code += 'GRPC_STATS_INC_HISTOGRAM((exec_ctx), GRPC_STATS_HISTOGRAM_%s, bucket);\n' % histogram.name.upper()
      code += 'return;\n'
      code += '}\n'
    code += 'GRPC_STATS_INC_HISTOGRAM((exec_ctx), GRPC_STATS_HISTOGRAM_%s, '% histogram.name.upper()
    code += 'grpc_stats_histo_find_bucket_slow((exec_ctx), value, grpc_stats_table_%d, %d));\n' % (bounds_idx, histogram.buckets)
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
  print >>H, "#ifdef __cplusplus"
  print >>H, "extern \"C\" {"
  print >>H, "#endif"
  print >>H

  for typename, instances in sorted(inst_map.items()):
    print >>H, "typedef enum {"
    for inst in instances:
      print >>H, "  GRPC_STATS_%s_%s," % (typename.upper(), inst.name.upper())
    print >>H, "  GRPC_STATS_%s_COUNT" % (typename.upper())
    print >>H, "} grpc_stats_%ss;" % (typename.lower())
    print >>H, "extern const char *grpc_stats_%s_name[GRPC_STATS_%s_COUNT];" % (
        typename.lower(), typename.upper())
    print >>H, "extern const char *grpc_stats_%s_doc[GRPC_STATS_%s_COUNT];" % (
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
  print >>H, "extern void (*const grpc_stats_inc_histogram[%d])(grpc_exec_ctx *exec_ctx, int x);" % len(inst_map['Histogram'])

  print >>H
  print >>H, "#ifdef __cplusplus"
  print >>H, "}"
  print >>H, "#endif"
  print >>H
  print >>H, "#endif /* GRPC_CORE_LIB_DEBUG_STATS_DATA_H */"

with open('src/core/lib/debug/stats_data.cc', 'w') as C:
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
  print >>C, "#include <grpc/support/useful.h>"

  histo_code = []
  for histogram in inst_map['Histogram']:
    code, bounds_idx = gen_bucket_code(histogram)
    histo_bucket_boundaries.append(bounds_idx)
    histo_code.append(code)

  for typename, instances in sorted(inst_map.items()):
    print >>C, "const char *grpc_stats_%s_name[GRPC_STATS_%s_COUNT] = {" % (
        typename.lower(), typename.upper())
    for inst in instances:
      print >>C, "  %s," % c_str(inst.name)
    print >>C, "};"
    print >>C, "const char *grpc_stats_%s_doc[GRPC_STATS_%s_COUNT] = {" % (
        typename.lower(), typename.upper())
    for inst in instances:
      print >>C, "  %s," % c_str(inst.doc)
    print >>C, "};"

  for i, tbl in enumerate(static_tables):
    print >>C, "const %s grpc_stats_table_%d[%d] = {%s};" % (
        tbl[0], i, len(tbl[1]), ','.join('%s' % x for x in tbl[1]))

  for histogram, code in zip(inst_map['Histogram'], histo_code):
    print >>C, ("void grpc_stats_inc_%s(grpc_exec_ctx *exec_ctx, int value) {%s}") % (
                histogram.name.lower(),
                code)

  print >>C, "const int grpc_stats_histo_buckets[%d] = {%s};" % (
      len(inst_map['Histogram']), ','.join('%s' % x for x in histo_buckets))
  print >>C, "const int grpc_stats_histo_start[%d] = {%s};" % (
      len(inst_map['Histogram']), ','.join('%s' % x for x in histo_start))
  print >>C, "const int *const grpc_stats_histo_bucket_boundaries[%d] = {%s};" % (
      len(inst_map['Histogram']), ','.join('grpc_stats_table_%d' % x for x in histo_bucket_boundaries))
  print >>C, "void (*const grpc_stats_inc_histogram[%d])(grpc_exec_ctx *exec_ctx, int x) = {%s};" % (
      len(inst_map['Histogram']), ','.join('grpc_stats_inc_%s' % histogram.name.lower() for histogram in inst_map['Histogram']))

# patch qps_test bigquery schema
RECORD_EXPLICIT_PERCENTILES = [50, 95, 99]

with open('tools/run_tests/performance/scenario_result_schema.json', 'r') as f:
  qps_schema = json.loads(f.read())

def FindNamed(js, name):
  for el in js:
    if el['name'] == name:
      return el

def RemoveCoreFields(js):
  new_fields = []
  for field in js['fields']:
    if not field['name'].startswith('core_'):
      new_fields.append(field)
  js['fields'] = new_fields

RemoveCoreFields(FindNamed(qps_schema, 'clientStats'))
RemoveCoreFields(FindNamed(qps_schema, 'serverStats'))

def AddCoreFields(js):
  for counter in inst_map['Counter']:
    js['fields'].append({
      'name': 'core_%s' % counter.name,
      'type': 'INTEGER',
      'mode': 'NULLABLE'
    })
  for histogram in inst_map['Histogram']:
    js['fields'].append({
      'name': 'core_%s' % histogram.name,
      'type': 'STRING',
      'mode': 'NULLABLE'
    })
    js['fields'].append({
      'name': 'core_%s_bkts' % histogram.name,
      'type': 'STRING',
      'mode': 'NULLABLE'
    })
    for pctl in RECORD_EXPLICIT_PERCENTILES:
      js['fields'].append({
        'name': 'core_%s_%dp' % (histogram.name, pctl),
        'type': 'FLOAT',
        'mode': 'NULLABLE'
      })

AddCoreFields(FindNamed(qps_schema, 'clientStats'))
AddCoreFields(FindNamed(qps_schema, 'serverStats'))

with open('tools/run_tests/performance/scenario_result_schema.json', 'w') as f:
  f.write(json.dumps(qps_schema, indent=2, sort_keys=True))

# and generate a helper script to massage scenario results into the format we'd
# like to query
with open('tools/run_tests/performance/massage_qps_stats.py', 'w') as P:
  with open(sys.argv[0]) as my_source:
    for line in my_source:
      if line[0] != '#': break
    for line in my_source:
      if line[0] == '#':
        print >>P, line.rstrip()
        break
    for line in my_source:
      if line[0] != '#':
        break
      print >>P, line.rstrip()

  print >>P
  print >>P, '# Autogenerated by tools/codegen/core/gen_stats_data.py'
  print >>P

  print >>P, 'import massage_qps_stats_helpers'

  print >>P, 'def massage_qps_stats(scenario_result):'
  print >>P, '  for stats in scenario_result["serverStats"] + scenario_result["clientStats"]:'
  print >>P, '    if "coreStats" not in stats: return'
  print >>P, '    core_stats = stats["coreStats"]'
  print >>P, '    del stats["coreStats"]'
  for counter in inst_map['Counter']:
    print >>P, '    stats["core_%s"] = massage_qps_stats_helpers.counter(core_stats, "%s")' % (counter.name, counter.name)
  for i, histogram in enumerate(inst_map['Histogram']):
    print >>P, '    h = massage_qps_stats_helpers.histogram(core_stats, "%s")' % histogram.name
    print >>P, '    stats["core_%s"] = ",".join("%%f" %% x for x in h.buckets)' % histogram.name
    print >>P, '    stats["core_%s_bkts"] = ",".join("%%f" %% x for x in h.boundaries)' % histogram.name
    for pctl in RECORD_EXPLICIT_PERCENTILES:
      print >>P, '    stats["core_%s_%dp"] = massage_qps_stats_helpers.percentile(h.buckets, %d, h.boundaries)' % (
          histogram.name, pctl, pctl)

with open('src/core/lib/debug/stats_data_bq_schema.sql', 'w') as S:
  columns = []
  for counter in inst_map['Counter']:
    columns.append(('%s_per_iteration' % counter.name, 'FLOAT'))
  print >>S, ',\n'.join('%s:%s' % x for x in columns)

