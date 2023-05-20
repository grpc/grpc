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
    attrs = yaml.load(f.read(), Loader=yaml.Loader)

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
Shape = collections.namedtuple('Shape', 'max buckets')

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


def u642dbl(d):
    return ctypes.c_double.from_buffer(ctypes.c_ulonglong(d)).value


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
        if best is None:
            best = (shift_bits, n, table_size)
        elif best[1] < n:
            best = (shift_bits, n, table_size)
    return best


def gen_map_table(mapped_bounds, shift_data):
    #print("gen_map_table(%s, %s)" % (mapped_bounds, shift_data))
    tbl = []
    cur = 0
    mapped_bounds = [x >> shift_data[0] for x in mapped_bounds]
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


def merge_cases(cases):
    l = len(cases)
    if l == 1:
        return cases[0][1]
    left_len = l // 2
    left = cases[0:left_len]
    right = cases[left_len:]
    return 'if (value < %d) {\n%s\n} else {\n%s\n}' % (
        left[-1][0], merge_cases(left), merge_cases(right))


def gen_bucket_code(shape):
    bounds = [0, 1]
    done_trivial = False
    done_unmapped = False
    first_nontrivial = None
    first_unmapped = None
    while len(bounds) < shape.buckets + 1:
        if len(bounds) == shape.buckets:
            nextb = int(shape.max)
        else:
            mul = math.pow(
                float(shape.max) / bounds[-1],
                1.0 / (shape.buckets + 1 - len(bounds)))
            nextb = int(math.ceil(bounds[-1] * mul))
        if nextb <= bounds[-1] + 1:
            nextb = bounds[-1] + 1
        elif not done_trivial:
            done_trivial = True
            first_nontrivial = len(bounds)
        bounds.append(nextb)
    bounds_idx = decl_static_table(bounds, 'int')
    #print first_nontrivial, shift_data, bounds
    #if shift_data is not None: print [hex(x >> shift_data[0]) for x in code_bounds[first_nontrivial:]]
    if first_nontrivial is None:
        return ('return grpc_core::Clamp(value, 0, %d);\n' % shape.max,
                bounds_idx)
    cases = [(0, 'return 0;'), (first_nontrivial, 'return value;')]
    if done_trivial:
        first_nontrivial_code = dbl2u64(first_nontrivial)
        last_code = first_nontrivial_code
        while True:
            code = ''
            first_nontrivial = u642dbl(first_nontrivial_code)
            code_bounds_index = None
            for i, b in enumerate(bounds):
                if b > first_nontrivial:
                    code_bounds_index = i
                    break
            code_bounds = [dbl2u64(x) - first_nontrivial_code for x in bounds]
            shift_data = find_ideal_shift(code_bounds[code_bounds_index:],
                                          65536)
            if not shift_data:
                break
            map_table = gen_map_table(code_bounds[code_bounds_index:],
                                      shift_data)
            if not map_table:
                break
            if map_table[-1] < 5:
                break
            map_table_idx = decl_static_table(
                [x + code_bounds_index for x in map_table],
                type_for_uint_table(map_table))
            last_code = (
                (len(map_table) - 1) << shift_data[0]) + first_nontrivial_code
            code += 'DblUint val;\n'
            code += 'val.dbl = value;\n'
            code += 'const int bucket = '
            code += 'kStatsTable%d[((val.uint - %dull) >> %d)];\n' % (
                map_table_idx, first_nontrivial_code, shift_data[0])
            code += 'return bucket - (value < kStatsTable%d[bucket]);' % bounds_idx
            cases.append((int(u642dbl(last_code)) + 1, code))
            first_nontrivial_code = last_code
        last = u642dbl(last_code) + 1
        for i, b in enumerate(bounds[:-2]):
            if bounds[i + 1] < last:
                continue
            cases.append((bounds[i + 1], 'return %d;' % i))
    cases.append((None, 'return %d;' % (len(bounds) - 2)))
    return (merge_cases(cases), bounds_idx)


# utility: print a big comment block into a set of files
def put_banner(files, banner):
    for f in files:
        for line in banner:
            print('// %s' % line, file=f)
        print(file=f)


shapes = set()
for histogram in inst_map['Histogram']:
    shapes.add(Shape(max=histogram.max, buckets=histogram.buckets))


def snake_to_pascal(name):
    return ''.join([x.capitalize() for x in name.split('_')])


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

    print("#ifndef GRPC_SRC_CORE_LIB_DEBUG_STATS_DATA_H", file=H)
    print("#define GRPC_SRC_CORE_LIB_DEBUG_STATS_DATA_H", file=H)
    print(file=H)
    print("#include <grpc/support/port_platform.h>", file=H)
    print("#include <atomic>", file=H)
    print("#include <memory>", file=H)
    print("#include <stdint.h>", file=H)
    print("#include \"src/core/lib/debug/histogram_view.h\"", file=H)
    print("#include \"absl/strings/string_view.h\"", file=H)
    print("#include \"src/core/lib/gprpp/per_cpu.h\"", file=H)
    print(file=H)
    print("namespace grpc_core {", file=H)

    for shape in shapes:
        print("class HistogramCollector_%d_%d;" % (shape.max, shape.buckets),
              file=H)
        print("class Histogram_%d_%d {" % (shape.max, shape.buckets), file=H)
        print(" public:", file=H)
        print("  static int BucketFor(int value);", file=H)
        print("  const uint64_t* buckets() const { return buckets_; }", file=H)
        print(
            "  friend Histogram_%d_%d operator-(const Histogram_%d_%d& left, const Histogram_%d_%d& right);"
            % (shape.max, shape.buckets, shape.max, shape.buckets, shape.max,
               shape.buckets),
            file=H)
        print(" private:", file=H)
        print("  friend class HistogramCollector_%d_%d;" %
              (shape.max, shape.buckets),
              file=H)
        print("  uint64_t buckets_[%d]{};" % shape.buckets, file=H)
        print("};", file=H)
        print("class HistogramCollector_%d_%d {" % (shape.max, shape.buckets),
              file=H)
        print(" public:", file=H)
        print("  void Increment(int value) {", file=H)
        print("    buckets_[Histogram_%d_%d::BucketFor(value)]" %
              (shape.max, shape.buckets),
              file=H)
        print("        .fetch_add(1, std::memory_order_relaxed);", file=H)
        print("  }", file=H)
        print("  void Collect(Histogram_%d_%d* result) const;" %
              (shape.max, shape.buckets),
              file=H)
        print(" private:", file=H)
        print("  std::atomic<uint64_t> buckets_[%d]{};" % shape.buckets, file=H)
        print("};", file=H)

    print("struct GlobalStats {", file=H)
    print("  enum class Counter {", file=H)
    for ctr in inst_map['Counter']:
        print("    k%s," % snake_to_pascal(ctr.name), file=H)
    print("    COUNT", file=H)
    print("  };", file=H)
    print("  enum class Histogram {", file=H)
    for ctr in inst_map['Histogram']:
        print("    k%s," % snake_to_pascal(ctr.name), file=H)
    print("    COUNT", file=H)
    print("  };", file=H)
    print("  GlobalStats();", file=H)
    print(
        "  static const absl::string_view counter_name[static_cast<int>(Counter::COUNT)];",
        file=H)
    print(
        "  static const absl::string_view histogram_name[static_cast<int>(Histogram::COUNT)];",
        file=H)
    print(
        "  static const absl::string_view counter_doc[static_cast<int>(Counter::COUNT)];",
        file=H)
    print(
        "  static const absl::string_view histogram_doc[static_cast<int>(Histogram::COUNT)];",
        file=H)
    print("  union {", file=H)
    print("    struct {", file=H)
    for ctr in inst_map['Counter']:
        print("    uint64_t %s;" % ctr.name, file=H)
    print("    };", file=H)
    print("    uint64_t counters[static_cast<int>(Counter::COUNT)];", file=H)
    print("  };", file=H)
    for ctr in inst_map['Histogram']:
        print("  Histogram_%d_%d %s;" % (ctr.max, ctr.buckets, ctr.name),
              file=H)
    print("  HistogramView histogram(Histogram which) const;", file=H)
    print(
        "  std::unique_ptr<GlobalStats> Diff(const GlobalStats& other) const;",
        file=H)
    print("};", file=H)
    print("class GlobalStatsCollector {", file=H)
    print(" public:", file=H)
    print("  std::unique_ptr<GlobalStats> Collect() const;", file=H)
    for ctr in inst_map['Counter']:
        print(
            "  void Increment%s() { data_.this_cpu().%s.fetch_add(1, std::memory_order_relaxed); }"
            % (snake_to_pascal(ctr.name), ctr.name),
            file=H)
    for ctr in inst_map['Histogram']:
        print(
            "  void Increment%s(int value) { data_.this_cpu().%s.Increment(value); }"
            % (snake_to_pascal(ctr.name), ctr.name),
            file=H)
    print(" private:", file=H)
    print("  struct Data {", file=H)
    for ctr in inst_map['Counter']:
        print("    std::atomic<uint64_t> %s{0};" % ctr.name, file=H)
    for ctr in inst_map['Histogram']:
        print("    HistogramCollector_%d_%d %s;" %
              (ctr.max, ctr.buckets, ctr.name),
              file=H)
    print("  };", file=H)
    print(
        "  PerCpu<Data> data_{PerCpuOptions().SetCpusPerShard(4).SetMaxShards(32)};",
        file=H)
    print("};", file=H)
    print("}", file=H)

    print(file=H)
    print("#endif // GRPC_SRC_CORE_LIB_DEBUG_STATS_DATA_H", file=H)

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
    print("#include \"src/core/lib/debug/stats_data.h\"", file=C)
    print("#include <stdint.h>", file=C)
    print(file=C)

    histo_code = []
    histo_bucket_boundaries = {}
    for shape in shapes:
        code, bounds_idx = gen_bucket_code(shape)
        histo_bucket_boundaries[shape] = bounds_idx
        histo_code.append(code)

    print("namespace grpc_core {", file=C)
    print("namespace { union DblUint { double dbl; uint64_t uint; }; }", file=C)

    for shape in shapes:
        print(
            "void HistogramCollector_%d_%d::Collect(Histogram_%d_%d* result) const {"
            % (shape.max, shape.buckets, shape.max, shape.buckets),
            file=C)
        print("  for (int i=0; i<%d; i++) {" % shape.buckets, file=C)
        print(
            "    result->buckets_[i] += buckets_[i].load(std::memory_order_relaxed);",
            file=C)
        print("  }", file=C)
        print("}", file=C)
        print(
            "Histogram_%d_%d operator-(const Histogram_%d_%d& left, const Histogram_%d_%d& right) {"
            % (shape.max, shape.buckets, shape.max, shape.buckets, shape.max,
               shape.buckets),
            file=C)
        print("  Histogram_%d_%d result;" % (shape.max, shape.buckets), file=C)
        print("  for (int i=0; i<%d; i++) {" % shape.buckets, file=C)
        print("    result.buckets_[i] = left.buckets_[i] - right.buckets_[i];",
              file=C)
        print("  }", file=C)
        print("  return result;", file=C)
        print("}", file=C)

    for typename, instances in sorted(inst_map.items()):
        print(
            "const absl::string_view GlobalStats::%s_name[static_cast<int>(%s::COUNT)] = {"
            % (typename.lower(), typename),
            file=C)
        for inst in instances:
            print("  %s," % c_str(inst.name), file=C)
        print("};", file=C)
        print(
            "const absl::string_view GlobalStats::%s_doc[static_cast<int>(%s::COUNT)] = {"
            % (typename.lower(), typename),
            file=C)
        for inst in instances:
            print("  %s," % c_str(inst.doc), file=C)
        print("};", file=C)

    print("namespace {", file=C)
    for i, tbl in enumerate(static_tables):
        print("const %s kStatsTable%d[%d] = {%s};" %
              (tbl[0], i, len(tbl[1]), ','.join('%s' % x for x in tbl[1])),
              file=C)
    print("}  // namespace", file=C)

    for shape, code in zip(shapes, histo_code):
        print(("int Histogram_%d_%d::BucketFor(int value) {%s}") %
              (shape.max, shape.buckets, code),
              file=C)

    print("GlobalStats::GlobalStats() : %s {}" %
          ",".join("%s{0}" % ctr.name for ctr in inst_map['Counter']),
          file=C)

    print("HistogramView GlobalStats::histogram(Histogram which) const {",
          file=C)
    print("  switch (which) {", file=C)
    print("    default: GPR_UNREACHABLE_CODE(return HistogramView());", file=C)
    for inst in inst_map['Histogram']:
        print("    case Histogram::k%s:" % snake_to_pascal(inst.name), file=C)
        print(
            "      return HistogramView{&Histogram_%d_%d::BucketFor, kStatsTable%d, %d, %s.buckets()};"
            % (inst.max, inst.buckets, histo_bucket_boundaries[Shape(
                inst.max, inst.buckets)], inst.buckets, inst.name),
            file=C)
    print("  }", file=C)
    print("}", file=C)

    print(
        "std::unique_ptr<GlobalStats> GlobalStatsCollector::Collect() const {",
        file=C)
    print("  auto result = std::make_unique<GlobalStats>();", file=C)
    print("  for (const auto& data : data_) {", file=C)
    for ctr in inst_map['Counter']:
        print("    result->%s += data.%s.load(std::memory_order_relaxed);" %
              (ctr.name, ctr.name),
              file=C)
    for h in inst_map['Histogram']:
        print("    data.%s.Collect(&result->%s);" % (h.name, h.name), file=C)
    print("  }", file=C)
    print("  return result;", file=C)
    print("}", file=C)

    print(
        "std::unique_ptr<GlobalStats> GlobalStats::Diff(const GlobalStats& other) const {",
        file=C)
    print("  auto result = std::make_unique<GlobalStats>();", file=C)
    for ctr in inst_map['Counter']:
        print("  result->%s = %s - other.%s;" % (ctr.name, ctr.name, ctr.name),
              file=C)
    for h in inst_map['Histogram']:
        print("  result->%s = %s - other.%s;" % (h.name, h.name, h.name),
              file=C)
    print("  return result;", file=C)
    print("}", file=C)

    print("}", file=C)
