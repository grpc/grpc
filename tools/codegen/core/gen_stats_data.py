#!/usr/bin/env python3

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

from __future__ import print_function

import collections
import ctypes
import json
import math
import sys

import yaml

with open('src/core/lib/debug/stats_data.yaml') as f:
    attrs = yaml.load(f.read())

REQUIRED_FIELDS = ['name', 'doc']


def make_type(name, fields):
    return (collections.namedtuple(
        name, ' '.join(list(set(REQUIRED_FIELDS + fields)))), [])


def c_str(s, encoding='ascii'):
    if isinstance(s, str):
        s = s.encode(encoding)
    result = ''
    for c in s:
        c = chr(c) if isinstance(c, int) else c
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
    for shift_bits in reversed(list(range(0, 64))):
        n = shift_works_until(mapped_bounds, shift_bits)
        if n == 0:
            continue
        table_size = mapped_bounds[n - 1] >> shift_bits
        if table_size > max_size:
            continue
        if table_size > 65535:
            continue
        if best is None:
            best = (shift_bits, n, table_size)
        elif best[1] < n:
            best = (shift_bits, n, table_size)
    print(best)
    return best


def gen_map_table(mapped_bounds, shift_data):
    tbl = []
    cur = 0
    print(mapped_bounds)
    mapped_bounds = [x >> shift_data[0] for x in mapped_bounds]
    print(mapped_bounds)
    for i in range(0, mapped_bounds[shift_data[1] - 1]):
        while i > mapped_bounds[cur]:
            cur += 1
        tbl.append(cur)
    return tbl


static_tables = []


def decl_static_table(values, type):
    global static_tables
    v = (type, values)
    for i, vp in enumerate(static_tables):
        if v == vp:
            return i
    print("ADD TABLE: %s %r" % (type, values))
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
            mul = math.pow(
                float(histogram.max) / bounds[-1],
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
        shift_data = find_ideal_shift(code_bounds[first_nontrivial:],
                                      256 * histogram.buckets)
    #print first_nontrivial, shift_data, bounds
    #if shift_data is not None: print [hex(x >> shift_data[0]) for x in code_bounds[first_nontrivial:]]
    code = 'value = grpc_core::Clamp(value, 0, %d);\n' % histogram.max
    map_table = gen_map_table(code_bounds[first_nontrivial:], shift_data)
    if first_nontrivial is None:
        code += ('GRPC_STATS_INC_HISTOGRAM(GRPC_STATS_HISTOGRAM_%s, value);\n' %
                 histogram.name.upper())
    else:
        code += 'if (value < %d) {\n' % first_nontrivial
        code += ('GRPC_STATS_INC_HISTOGRAM(GRPC_STATS_HISTOGRAM_%s, value);\n' %
                 histogram.name.upper())
        code += 'return;\n'
        code += '}'
        first_nontrivial_code = dbl2u64(first_nontrivial)
        if shift_data is not None:
            map_table_idx = decl_static_table(map_table,
                                              type_for_uint_table(map_table))
            code += 'union { double dbl; uint64_t uint; } _val, _bkt;\n'
            code += '_val.dbl = value;\n'
            code += 'if (_val.uint < %dull) {\n' % (
                (map_table[-1] << shift_data[0]) + first_nontrivial_code)
            code += 'int bucket = '
            code += 'grpc_stats_table_%d[((_val.uint - %dull) >> %d)] + %d;\n' % (
                map_table_idx, first_nontrivial_code, shift_data[0],
                first_nontrivial)
            code += '_bkt.dbl = grpc_stats_table_%d[bucket];\n' % bounds_idx
            code += 'bucket -= (_val.uint < _bkt.uint);\n'
            code += 'GRPC_STATS_INC_HISTOGRAM(GRPC_STATS_HISTOGRAM_%s, bucket);\n' % histogram.name.upper(
            )
            code += 'return;\n'
            code += '}\n'
        code += 'GRPC_STATS_INC_HISTOGRAM(GRPC_STATS_HISTOGRAM_%s, ' % histogram.name.upper(
        )
        code += 'grpc_stats_histo_find_bucket_slow(value, grpc_stats_table_%d, %d));\n' % (
            bounds_idx, histogram.buckets)
    return (code, bounds_idx)


# utility: print a big comment block into a set of files
def put_banner(files, banner):
    for f in files:
        print('/*', file=f)
        for line in banner:
            print(' * %s' % line, file=f)
        print(' */', file=f)
        print(file=f)


with open('src/core/lib/debug/stats_data.h', 'w') as H:
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
        put_banner([H], [line[2:].rstrip() for line in copyright])

    put_banner(
        [H],
        ["Automatically generated by tools/codegen/core/gen_stats_data.py"])

    print("#ifndef GRPC_CORE_LIB_DEBUG_STATS_DATA_H", file=H)
    print("#define GRPC_CORE_LIB_DEBUG_STATS_DATA_H", file=H)
    print(file=H)
    print("#include <grpc/support/port_platform.h>", file=H)
    print(file=H)
    print("#include <inttypes.h>", file=H)
    print("#include \"src/core/lib/iomgr/exec_ctx.h\"", file=H)
    print(file=H)

    for typename, instances in sorted(inst_map.items()):
        print("typedef enum {", file=H)
        for inst in instances:
            print("  GRPC_STATS_%s_%s," % (typename.upper(), inst.name.upper()),
                  file=H)
        print("  GRPC_STATS_%s_COUNT" % (typename.upper()), file=H)
        print("} grpc_stats_%ss;" % (typename.lower()), file=H)
        print("extern const char *grpc_stats_%s_name[GRPC_STATS_%s_COUNT];" %
              (typename.lower(), typename.upper()),
              file=H)
        print("extern const char *grpc_stats_%s_doc[GRPC_STATS_%s_COUNT];" %
              (typename.lower(), typename.upper()),
              file=H)

    histo_start = []
    histo_buckets = []
    histo_bucket_boundaries = []

    print("typedef enum {", file=H)
    first_slot = 0
    for histogram in inst_map['Histogram']:
        histo_start.append(first_slot)
        histo_buckets.append(histogram.buckets)
        print("  GRPC_STATS_HISTOGRAM_%s_FIRST_SLOT = %d," %
              (histogram.name.upper(), first_slot),
              file=H)
        print("  GRPC_STATS_HISTOGRAM_%s_BUCKETS = %d," %
              (histogram.name.upper(), histogram.buckets),
              file=H)
        first_slot += histogram.buckets
    print("  GRPC_STATS_HISTOGRAM_BUCKETS = %d" % first_slot, file=H)
    print("} grpc_stats_histogram_constants;", file=H)

    print("#if defined(GRPC_COLLECT_STATS) || !defined(NDEBUG)", file=H)
    for ctr in inst_map['Counter']:
        print(("#define GRPC_STATS_INC_%s() " +
               "GRPC_STATS_INC_COUNTER(GRPC_STATS_COUNTER_%s)") %
              (ctr.name.upper(), ctr.name.upper()),
              file=H)
    for histogram in inst_map['Histogram']:
        print(
            "#define GRPC_STATS_INC_%s(value) grpc_stats_inc_%s( (int)(value))"
            % (histogram.name.upper(), histogram.name.lower()),
            file=H)
        print("void grpc_stats_inc_%s(int x);" % histogram.name.lower(), file=H)

    print("#else", file=H)
    for ctr in inst_map['Counter']:
        print(("#define GRPC_STATS_INC_%s() ") % (ctr.name.upper()), file=H)
    for histogram in inst_map['Histogram']:
        print("#define GRPC_STATS_INC_%s(value)" % (histogram.name.upper()),
              file=H)
    print("#endif /* defined(GRPC_COLLECT_STATS) || !defined(NDEBUG) */",
          file=H)

    for i, tbl in enumerate(static_tables):
        print("extern const %s grpc_stats_table_%d[%d];" %
              (tbl[0], i, len(tbl[1])),
              file=H)

    print("extern const int grpc_stats_histo_buckets[%d];" %
          len(inst_map['Histogram']),
          file=H)
    print("extern const int grpc_stats_histo_start[%d];" %
          len(inst_map['Histogram']),
          file=H)
    print("extern const int *const grpc_stats_histo_bucket_boundaries[%d];" %
          len(inst_map['Histogram']),
          file=H)
    print("extern void (*const grpc_stats_inc_histogram[%d])(int x);" %
          len(inst_map['Histogram']),
          file=H)

    print(file=H)
    print("#endif /* GRPC_CORE_LIB_DEBUG_STATS_DATA_H */", file=H)

with open('src/core/lib/debug/stats_data.cc', 'w') as C:
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
        put_banner([C], [line[2:].rstrip() for line in copyright])

    put_banner(
        [C],
        ["Automatically generated by tools/codegen/core/gen_stats_data.py"])

    print("#include <grpc/support/port_platform.h>", file=C)
    print(file=C)
    print("#include \"src/core/lib/debug/stats.h\"", file=C)
    print("#include \"src/core/lib/debug/stats_data.h\"", file=C)
    print("#include \"src/core/lib/gpr/useful.h\"", file=C)
    print("#include \"src/core/lib/iomgr/exec_ctx.h\"", file=C)
    print(file=C)

    histo_code = []
    for histogram in inst_map['Histogram']:
        code, bounds_idx = gen_bucket_code(histogram)
        histo_bucket_boundaries.append(bounds_idx)
        histo_code.append(code)

    for typename, instances in sorted(inst_map.items()):
        print("const char *grpc_stats_%s_name[GRPC_STATS_%s_COUNT] = {" %
              (typename.lower(), typename.upper()),
              file=C)
        for inst in instances:
            print("  %s," % c_str(inst.name), file=C)
        print("};", file=C)
        print("const char *grpc_stats_%s_doc[GRPC_STATS_%s_COUNT] = {" %
              (typename.lower(), typename.upper()),
              file=C)
        for inst in instances:
            print("  %s," % c_str(inst.doc), file=C)
        print("};", file=C)

    for i, tbl in enumerate(static_tables):
        print("const %s grpc_stats_table_%d[%d] = {%s};" %
              (tbl[0], i, len(tbl[1]), ','.join('%s' % x for x in tbl[1])),
              file=C)

    for histogram, code in zip(inst_map['Histogram'], histo_code):
        print(("void grpc_stats_inc_%s(int value) {%s}") %
              (histogram.name.lower(), code),
              file=C)

    print(
        "const int grpc_stats_histo_buckets[%d] = {%s};" %
        (len(inst_map['Histogram']), ','.join('%s' % x for x in histo_buckets)),
        file=C)
    print("const int grpc_stats_histo_start[%d] = {%s};" %
          (len(inst_map['Histogram']), ','.join('%s' % x for x in histo_start)),
          file=C)
    print("const int *const grpc_stats_histo_bucket_boundaries[%d] = {%s};" %
          (len(inst_map['Histogram']), ','.join(
              'grpc_stats_table_%d' % x for x in histo_bucket_boundaries)),
          file=C)
    print("void (*const grpc_stats_inc_histogram[%d])(int x) = {%s};" %
          (len(inst_map['Histogram']), ','.join(
              'grpc_stats_inc_%s' % histogram.name.lower()
              for histogram in inst_map['Histogram'])),
          file=C)

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
            if line[0] != '#':
                break
        for line in my_source:
            if line[0] == '#':
                print(line.rstrip(), file=P)
                break
        for line in my_source:
            if line[0] != '#':
                break
            print(line.rstrip(), file=P)

    print(file=P)
    print('# Autogenerated by tools/codegen/core/gen_stats_data.py', file=P)
    print(file=P)

    print('import massage_qps_stats_helpers', file=P)

    print('def massage_qps_stats(scenario_result):', file=P)
    print(
        '  for stats in scenario_result["serverStats"] + scenario_result["clientStats"]:',
        file=P)
    print('    if "coreStats" in stats:', file=P)
    print(
        '      # Get rid of the "coreStats" element and replace it by statistics',
        file=P)
    print('      # that correspond to columns in the bigquery schema.', file=P)
    print('      core_stats = stats["coreStats"]', file=P)
    print('      del stats["coreStats"]', file=P)
    for counter in inst_map['Counter']:
        print(
            '      stats["core_%s"] = massage_qps_stats_helpers.counter(core_stats, "%s")'
            % (counter.name, counter.name),
            file=P)
    for i, histogram in enumerate(inst_map['Histogram']):
        print(
            '      h = massage_qps_stats_helpers.histogram(core_stats, "%s")' %
            histogram.name,
            file=P)
        print(
            '      stats["core_%s"] = ",".join("%%f" %% x for x in h.buckets)' %
            histogram.name,
            file=P)
        print(
            '      stats["core_%s_bkts"] = ",".join("%%f" %% x for x in h.boundaries)'
            % histogram.name,
            file=P)
        for pctl in RECORD_EXPLICIT_PERCENTILES:
            print(
                '      stats["core_%s_%dp"] = massage_qps_stats_helpers.percentile(h.buckets, %d, h.boundaries)'
                % (histogram.name, pctl, pctl),
                file=P)

with open('src/core/lib/debug/stats_data_bq_schema.sql', 'w') as S:
    columns = []
    for counter in inst_map['Counter']:
        columns.append(('%s_per_iteration' % counter.name, 'FLOAT'))
    print(',\n'.join('%s:%s' % x for x in columns), file=S)
