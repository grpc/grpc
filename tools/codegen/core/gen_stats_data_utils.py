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
import math
import sys

_REQUIRED_FIELDS = [
    "name",
    "doc",
    "scope",
    "linked_global_scope",
]


def make_type(name, fields, defaults):
    return (
        collections.namedtuple(
            name, " ".join(_REQUIRED_FIELDS + fields), defaults=defaults
        ),
        [],
    )


def c_str(s, encoding="ascii"):
    if isinstance(s, str):
        s = s.encode(encoding)
    result = ""
    for c in s:
        c = chr(c) if isinstance(c, int) else c
        if not (32 <= ord(c) < 127) or c in ("\\", '"'):
            result += "\\%03o" % ord(c)
        else:
            result += c
    return '"' + result + '"'


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
    # print("gen_map_table(%s, %s)" % (mapped_bounds, shift_data))
    tbl = []
    cur = 0
    mapped_bounds = [x >> shift_data[0] for x in mapped_bounds]
    for i in range(0, mapped_bounds[shift_data[1] - 1]):
        while i > mapped_bounds[cur]:
            cur += 1
        tbl.append(cur)
    return tbl


def type_for_uint_table(table):
    mv = max(table)
    if mv < 2**8:
        return "uint8_t"
    elif mv < 2**16:
        return "uint16_t"
    elif mv < 2**32:
        return "uint32_t"
    else:
        return "uint64_t"


def merge_cases(cases):
    l = len(cases)
    if l == 1:
        return cases[0][1]
    left_len = l // 2
    left = cases[0:left_len]
    right = cases[left_len:]
    return "if (value < %d) {\n%s\n} else {\n%s\n}" % (
        left[-1][0],
        merge_cases(left),
        merge_cases(right),
    )


# utility: print a big comment block into a set of files
def put_banner(files, banner):
    for f in files:
        for line in banner:
            print("// %s" % line, file=f)
        print(file=f)


def snake_to_pascal(name):
    return "".join([x.capitalize() for x in name.split("_")])


Shape = collections.namedtuple("Shape", "max buckets bits")


def shape_signature(shape):
    return "%d_%d_%d" % (shape.max, shape.buckets, shape.bits)


def histogram_shape(histogram, global_scope):
    if global_scope:
        return Shape(histogram.max, histogram.buckets, 64)
    else:
        return Shape(
            histogram.max,
            (
                histogram.scope_buckets
                if histogram.scope_buckets
                else histogram.buckets
            ),
            histogram.scope_counter_bits,
        )


def histogram_shape_signature(histogram, global_scope):
    return shape_signature(histogram_shape(histogram, global_scope))


class DefaultLocalScopedStatsCollectorGenerator:
    """Generate StatsCollector classes for a given local scope."""

    def __init__(self, scope, linked_global_scope="global"):
        self._scope = scope
        self._linked_global_scope = linked_global_scope

    def generate_stats_collector(self, class_name, inst_map, H):
        print(" public:", file=H)
        print(
            "  const %s& View() const { return data_; };" % class_name,
            file=H,
        )
        for ctr in inst_map["Counter"]:
            if (
                ctr.scope != self._scope
                and ctr.scope != self._linked_global_scope
            ):
                continue
            if ctr.scope == self._linked_global_scope:
                print(
                    "  void Increment%s() { %s_stats().Increment%s(); }"
                    % (
                        snake_to_pascal(ctr.name),
                        self._linked_global_scope,
                        snake_to_pascal(ctr.name),
                    ),
                    file=H,
                )
            else:
                print(
                    "  void Increment%s() { ++data_.%s; %s_stats().Increment%s(); }"
                    % (
                        snake_to_pascal(ctr.name),
                        ctr.name,
                        self._linked_global_scope,
                        snake_to_pascal(ctr.name),
                    ),
                    file=H,
                )
        for ctr in inst_map["Histogram"]:
            if (
                ctr.scope != self._scope
                and ctr.scope != self._linked_global_scope
            ):
                continue
            if ctr.scope == self._linked_global_scope:
                print(
                    "  void Increment%s(int value) { "
                    " %s_stats().Increment%s(value); }"
                    % (
                        snake_to_pascal(ctr.name),
                        self._linked_global_scope,
                        snake_to_pascal(ctr.name),
                    ),
                    file=H,
                )
            else:
                print(
                    "  void Increment%s(int value) { data_.%s.Increment(value);"
                    " %s_stats().Increment%s(value); }"
                    % (
                        snake_to_pascal(ctr.name),
                        ctr.name,
                        self._linked_global_scope,
                        snake_to_pascal(ctr.name),
                    ),
                    file=H,
                )

        print(" private:", file=H)
        print("  %s data_;" % class_name, file=H)


class StatsDataGenerator:
    """Generates stats_data.h and stats_data.cc."""

    def __init__(self, attrs):
        self._attrs = attrs
        self._types = (
            make_type("Counter", [], []),
            make_type(
                "Histogram",
                ["max", "buckets", "scope_counter_bits", "scope_buckets"],
                [64, 0],
            ),
        )
        self._inst_map = dict((t[0].__name__, t[1]) for t in self._types)
        self._static_tables = []
        self._shapes = set()
        self._scopes = set()
        self._linked_global_scopes = {}
        for attr in self._attrs:
            found = False
            for t, lst in self._types:
                t_name = t.__name__.lower()
                if t_name in attr:
                    name = attr[t_name]
                    del attr[t_name]
                    lst.append(t(name=name, **attr))
                    self._scopes.add(attr["scope"])
                    self._linked_global_scopes[attr["scope"]] = attr[
                        "linked_global_scope"
                    ]
                    found = True
                    break
            assert found, "Bad decl: %s" % attr
        for histogram in self._inst_map["Histogram"]:
            self._shapes.add(histogram_shape(histogram, True))
            if histogram.scope != histogram.linked_global_scope:
                self._shapes.add(histogram_shape(histogram, False))
        # make self._scopes a sorted list, but with global at the start
        assert "global" in self._scopes
        global_scopes = []
        for scope in self._scopes:
            if scope == self._linked_global_scopes[scope]:
                global_scopes.append(scope)
        for scope in global_scopes:
            self._scopes.remove(scope)
        self._scopes = sorted(global_scopes) + sorted(self._scopes)
        self._scoped_stats_collector_generators = {}

    def register_scoped_stats_collector_generator(
        self, scope, linked_global_scope, generator
    ):
        if scope not in self._scoped_stats_collector_generators:
            self._scoped_stats_collector_generators[scope] = {}
        self._scoped_stats_collector_generators[scope][
            linked_global_scope
        ] = generator

    def _decl_static_table(self, values, type):
        v = (type, values)
        for i, vp in enumerate(self._static_tables):
            if v == vp:
                return i
        r = len(self._static_tables)
        self._static_tables.append(v)
        return r

    def _gen_bucket_code(self, shape):
        bounds = [0, 1]
        done_trivial = False
        first_nontrivial = None
        while len(bounds) < shape.buckets + 1:
            if len(bounds) == shape.buckets:
                nextb = int(shape.max)
            else:
                mul = math.pow(
                    float(shape.max) / bounds[-1],
                    1.0 / (shape.buckets + 1 - len(bounds)),
                )
                nextb = int(math.ceil(bounds[-1] * mul))
            if nextb <= bounds[-1] + 1:
                nextb = bounds[-1] + 1
            elif not done_trivial:
                done_trivial = True
                first_nontrivial = len(bounds)
            bounds.append(nextb)
        bounds_idx = self._decl_static_table(bounds, "int")
        # print first_nontrivial, shift_data, bounds
        # if shift_data is not None: print [hex(x >> shift_data[0]) for x in
        # code_bounds[first_nontrivial:]]
        if first_nontrivial is None:
            return (
                "return grpc_core::Clamp(value, 0, %d);\n" % shape.max,
                bounds_idx,
            )
        cases = [(0, "return 0;"), (first_nontrivial, "return value;")]
        if done_trivial:
            first_nontrivial_code = dbl2u64(first_nontrivial)
            last_code = first_nontrivial_code
            while True:
                code = ""
                first_nontrivial = u642dbl(first_nontrivial_code)
                code_bounds_index = None
                for i, b in enumerate(bounds):
                    if b > first_nontrivial:
                        code_bounds_index = i
                        break
                code_bounds = [
                    dbl2u64(x) - first_nontrivial_code for x in bounds
                ]
                shift_data = find_ideal_shift(
                    code_bounds[code_bounds_index:], 65536
                )
                if not shift_data:
                    break
                map_table = gen_map_table(
                    code_bounds[code_bounds_index:], shift_data
                )
                if not map_table:
                    break
                if map_table[-1] < 5:
                    break
                map_table_idx = self._decl_static_table(
                    [x + code_bounds_index for x in map_table],
                    type_for_uint_table(map_table),
                )
                last_code = (
                    (len(map_table) - 1) << shift_data[0]
                ) + first_nontrivial_code
                code += "DblUint val;\n"
                code += "val.dbl = value;\n"
                code += "const int bucket = "
                code += "kStatsTable%d[((val.uint - %dull) >> %d)];\n" % (
                    map_table_idx,
                    first_nontrivial_code,
                    shift_data[0],
                )
                code += (
                    "return bucket - (value < kStatsTable%d[bucket]);"
                    % bounds_idx
                )
                cases.append((int(u642dbl(last_code)) + 1, code))
                first_nontrivial_code = last_code
            last = u642dbl(last_code) + 1
            for i, b in enumerate(bounds[:-2]):
                if bounds[i + 1] < last:
                    continue
                cases.append((bounds[i + 1], "return %d;" % i))
        cases.append((None, "return %d;" % (len(bounds) - 2)))
        return (merge_cases(cases), bounds_idx)

    def gen_stats_data_hdr(self, prefix, header_file_path):
        """Generates the stats data header file."""
        with open(header_file_path, "w") as H:
            # copy-paste copyright notice from this file
            with open(sys.argv[0]) as my_source:
                copyright = []
                for line in my_source:
                    if line[0] != "#":
                        break
                for line in my_source:
                    if line[0] == "#":
                        copyright.append(line)
                        break
                for line in my_source:
                    if line[0] != "#":
                        break
                    copyright.append(line)
                put_banner([H], [line[2:].rstrip() for line in copyright])

            put_banner(
                [H],
                [
                    "Automatically generated by tools/codegen/core/gen_stats_data.py"
                ],
            )

            print("#ifndef GRPC_SRC_CORE_TELEMETRY_STATS_DATA_H", file=H)
            print("#define GRPC_SRC_CORE_TELEMETRY_STATS_DATA_H", file=H)
            print(file=H)
            print("#include <grpc/support/port_platform.h>", file=H)
            print("#include <atomic>", file=H)
            print("#include <memory>", file=H)
            print("#include <stdint.h>", file=H)
            print('#include "src/core/telemetry/histogram_view.h"', file=H)
            print(f'#include "{prefix}absl/strings/string_view.h"', file=H)
            print('#include "src/core/util/per_cpu.h"', file=H)
            print('#include "src/core/util/no_destruct.h"', file=H)
            print(file=H)
            print("namespace grpc_core {", file=H)

            for scope in self._scopes:
                print(
                    "class %sStatsCollector;" % snake_to_pascal(scope), file=H
                )

            for shape in self._shapes:
                if shape.bits == 64:
                    print(
                        "class HistogramCollector_%s;" % shape_signature(shape),
                        file=H,
                    )
                print(
                    "class Histogram_%s {" % shape_signature(shape),
                    file=H,
                )
                print(" public:", file=H)
                print("  static int BucketFor(int value);", file=H)
                print(
                    "  const uint%d_t* buckets() const { return buckets_; }"
                    % shape.bits,
                    file=H,
                )
                print(
                    "  size_t bucket_count() const { return %d; }"
                    % shape.buckets,
                    file=H,
                )
                print("  void Increment(int value) {", file=H)
                if shape.bits == 64:
                    print(
                        "    ++buckets_[Histogram_%s::BucketFor(value)];"
                        % shape_signature(shape),
                        file=H,
                    )
                else:
                    print(
                        "    auto& bucket = buckets_[Histogram_%s::BucketFor(value)];"
                        % shape_signature(shape),
                        file=H,
                    )
                    print(
                        "    if (GPR_UNLIKELY(bucket == std::numeric_limits<uint%d_t>::max())) {"
                        % shape.bits,
                        file=H,
                    )
                    print(
                        "      for (size_t i=0; i<%d; ++i) {" % shape.buckets,
                        file=H,
                    )
                    print("        buckets_[i] /= 2;", file=H)
                    print("      }", file=H)
                    print("    }", file=H)
                    print("    ++bucket;", file=H)
                print("  }", file=H)
                if shape.bits == 64:
                    print(
                        "  friend Histogram_%s operator-(const Histogram_%s& left,"
                        " const Histogram_%s& right);"
                        % (
                            shape_signature(shape),
                            shape_signature(shape),
                            shape_signature(shape),
                        ),
                        file=H,
                    )
                print(" private:", file=H)
                if shape.bits == 64:
                    print(
                        "  friend class HistogramCollector_%s;"
                        % shape_signature(shape),
                        file=H,
                    )
                print(
                    "  uint%d_t buckets_[%d]{};" % (shape.bits, shape.buckets),
                    file=H,
                )
                print("};", file=H)

                if shape.bits == 64:
                    print(
                        "class HistogramCollector_%s {"
                        % shape_signature(shape),
                        file=H,
                    )
                    print(" public:", file=H)
                    print("  void Increment(int value) {", file=H)
                    print(
                        "    buckets_[Histogram_%s::BucketFor(value)]"
                        % shape_signature(shape),
                        file=H,
                    )
                    print(
                        "        .fetch_add(1, std::memory_order_relaxed);",
                        file=H,
                    )
                    print("  }", file=H)
                    print(
                        "  void Collect(Histogram_%s* result) const;"
                        % shape_signature(shape),
                        file=H,
                    )
                    print(" private:", file=H)
                    print(
                        "  std::atomic<uint64_t> buckets_[%d]{};"
                        % shape.buckets,
                        file=H,
                    )
                    print("};", file=H)

            for scope in self._scopes:
                linked_global_scope = self._linked_global_scopes[scope]
                include_ctr = (
                    lambda ctr: ctr.scope == scope
                    or ctr.linked_global_scope == scope
                )

                class_name = snake_to_pascal(scope) + "Stats"
                print("struct %s {" % class_name, file=H)
                print("  enum class Counter {", file=H)
                for ctr in self._inst_map["Counter"]:
                    if not include_ctr(ctr):
                        continue
                    print("    k%s," % snake_to_pascal(ctr.name), file=H)
                print("    COUNT", file=H)
                print("  };", file=H)
                print("  enum class Histogram {", file=H)
                for ctr in self._inst_map["Histogram"]:
                    if not include_ctr(ctr):
                        continue
                    print("    k%s," % snake_to_pascal(ctr.name), file=H)
                print("    COUNT", file=H)
                print("  };", file=H)
                print("  %s();" % class_name, file=H)
                print(
                    (
                        "  static const absl::string_view"
                        " counter_name[static_cast<int>(Counter::COUNT)];"
                    ),
                    file=H,
                )
                print(
                    (
                        "  static const absl::string_view"
                        " histogram_name[static_cast<int>(Histogram::COUNT)];"
                    ),
                    file=H,
                )
                print(
                    (
                        "  static const absl::string_view"
                        " counter_doc[static_cast<int>(Counter::COUNT)];"
                    ),
                    file=H,
                )
                print(
                    (
                        "  static const absl::string_view"
                        " histogram_doc[static_cast<int>(Histogram::COUNT)];"
                    ),
                    file=H,
                )
                print("  union {", file=H)
                print("    struct {", file=H)
                for ctr in self._inst_map["Counter"]:
                    if not include_ctr(ctr):
                        continue
                    print("    uint64_t %s;" % ctr.name, file=H)
                print("    };", file=H)
                print(
                    "    uint64_t counters[static_cast<int>(Counter::COUNT)];",
                    file=H,
                )
                print("  };", file=H)
                for ctr in self._inst_map["Histogram"]:
                    if not include_ctr(ctr):
                        continue
                    print(
                        "  Histogram_%s %s;"
                        % (
                            histogram_shape_signature(
                                ctr, scope == linked_global_scope
                            ),
                            ctr.name,
                        ),
                        file=H,
                    )
                # Check if the scope is of type 'global'
                if scope == linked_global_scope:
                    print(
                        "  HistogramView histogram(Histogram which) const;",
                        file=H,
                    )
                    print(
                        "  std::unique_ptr<%s> Diff(const %s& other) const;"
                        % (class_name, class_name),
                        file=H,
                    )
                print("};", file=H)

                print("class %sCollector {" % class_name, file=H)
                if scope == linked_global_scope:
                    print(" public:", file=H)
                    print(
                        "  std::unique_ptr<%s> Collect() const;" % class_name,
                        file=H,
                    )
                    is_private = False

                    def set_private(yes):
                        nonlocal is_private
                        if is_private == yes:
                            return
                        is_private = yes
                        if yes:
                            print(" private:", file=H)
                        else:
                            print(" public:", file=H)

                    for ctr in self._inst_map["Counter"]:
                        if not include_ctr(ctr):
                            continue
                        set_private(ctr.scope != scope)
                        print(
                            "  void Increment%s() { data_.this_cpu().%s.fetch_add(1,"
                            " std::memory_order_relaxed); }"
                            % (snake_to_pascal(ctr.name), ctr.name),
                            file=H,
                        )
                    for ctr in self._inst_map["Histogram"]:
                        if not include_ctr(ctr):
                            continue
                        set_private(ctr.scope != scope)
                        print(
                            "  void Increment%s(int value) {"
                            " data_.this_cpu().%s.Increment(value); }"
                            % (snake_to_pascal(ctr.name), ctr.name),
                            file=H,
                        )
                    set_private(True)
                    for other_scope in self._scopes:
                        if other_scope == scope:
                            continue
                        print(
                            "  friend class %sStatsCollector;"
                            % snake_to_pascal(other_scope),
                            file=H,
                        )
                    print("  struct Data {", file=H)
                    for ctr in self._inst_map["Counter"]:
                        if not include_ctr(ctr):
                            continue
                        print(
                            "    std::atomic<uint64_t> %s{0};" % ctr.name,
                            file=H,
                        )
                    for ctr in self._inst_map["Histogram"]:
                        if not include_ctr(ctr):
                            continue
                        print(
                            "    HistogramCollector_%s %s;"
                            % (
                                histogram_shape_signature(
                                    ctr, scope == linked_global_scope
                                ),
                                ctr.name,
                            ),
                            file=H,
                        )
                    print("  };", file=H)
                    print(
                        (
                            "  PerCpu<Data>"
                            " data_{PerCpuOptions().SetCpusPerShard(4).SetMaxShards(32)};"
                        ),
                        file=H,
                    )
                else:  # not global
                    if scope not in self._scoped_stats_collector_generators:
                        generator = DefaultLocalScopedStatsCollectorGenerator(
                            scope, linked_global_scope
                        )
                    else:
                        generator = self._scoped_stats_collector_generators[
                            scope
                        ][linked_global_scope]
                    generator.generate_stats_collector(
                        class_name, self._inst_map, H
                    )

                print("};", file=H)
                if scope == linked_global_scope:
                    print(
                        "inline %sStatsCollector& %s_stats() {"
                        % (
                            snake_to_pascal(scope),
                            scope,
                        ),
                        file=H,
                    )
                    print(
                        "  return"
                        " *NoDestructSingleton<%sStatsCollector>::Get();"
                        % snake_to_pascal(scope),
                        file=H,
                    )
                    print("}", file=H)

            print("}", file=H)

            print(file=H)
            print("#endif // GRPC_SRC_CORE_TELEMETRY_STATS_DATA_H", file=H)

    def gen_stats_data_src(self, source_file_path):
        """Generates the stats data C++ file."""
        with open(source_file_path, "w") as C:
            # copy-paste copyright notice from this file
            with open(sys.argv[0]) as my_source:
                copyright = []
                for line in my_source:
                    if line[0] != "#":
                        break
                for line in my_source:
                    if line[0] == "#":
                        copyright.append(line)
                        break
                for line in my_source:
                    if line[0] != "#":
                        break
                    copyright.append(line)
                put_banner([C], [line[2:].rstrip() for line in copyright])

            put_banner(
                [C],
                [
                    "Automatically generated by tools/codegen/core/gen_stats_data.py"
                ],
            )

            print("#include <grpc/support/port_platform.h>", file=C)
            print(file=C)
            print('#include "src/core/telemetry/stats_data.h"', file=C)
            print("#include <stdint.h>", file=C)
            print(file=C)

            histo_code = []
            histo_bucket_boundaries = {}
            for shape in self._shapes:
                code, bounds_idx = self._gen_bucket_code(shape)
                histo_bucket_boundaries[shape] = bounds_idx
                histo_code.append(code)

            print("namespace grpc_core {", file=C)
            print(
                "namespace { union DblUint { double dbl; uint64_t uint; }; }",
                file=C,
            )

            for shape in self._shapes:
                if shape.bits == 64:
                    print(
                        "void HistogramCollector_%s::Collect(Histogram_%s* result)"
                        " const {"
                        % (shape_signature(shape), shape_signature(shape)),
                        file=C,
                    )
                    print(
                        "  for (int i=0; i<%d; i++) {" % shape.buckets, file=C
                    )
                    print(
                        (
                            "    result->buckets_[i] +="
                            " buckets_[i].load(std::memory_order_relaxed);"
                        ),
                        file=C,
                    )
                    print("  }", file=C)
                    print("}", file=C)
                    print(
                        "Histogram_%s operator-(const Histogram_%s& left, const"
                        " Histogram_%s& right) {"
                        % (
                            shape_signature(shape),
                            shape_signature(shape),
                            shape_signature(shape),
                        ),
                        file=C,
                    )
                    print(
                        "  Histogram_%s result;" % shape_signature(shape),
                        file=C,
                    )
                    print(
                        "  for (int i=0; i<%d; i++) {" % shape.buckets, file=C
                    )
                    print(
                        "    result.buckets_[i] = left.buckets_[i] - right.buckets_[i];",
                        file=C,
                    )
                    print("  }", file=C)
                    print("  return result;", file=C)
                    print("}", file=C)

            print("namespace {", file=C)
            for i, tbl in enumerate(self._static_tables):
                print(
                    "const %s kStatsTable%d[%d] = {%s};"
                    % (
                        tbl[0],
                        i,
                        len(tbl[1]),
                        ",".join("%s" % x for x in tbl[1]),
                    ),
                    file=C,
                )
            print("}  // namespace", file=C)

            for shape, code in zip(self._shapes, histo_code):
                print(
                    "int Histogram_%s::BucketFor(int value) {%s}"
                    % (shape_signature(shape), code),
                    file=C,
                )

            for scope in self._scopes:
                linked_global_scope = self._linked_global_scopes[scope]
                include_ctr = (
                    lambda ctr: ctr.scope == scope
                    or ctr.linked_global_scope == scope
                )
                class_name = snake_to_pascal(scope) + "Stats"
                for typename, instances in sorted(self._inst_map.items()):
                    print(
                        "const absl::string_view"
                        " %s::%s_name[static_cast<int>(%s::COUNT)] = {"
                        % (class_name, typename.lower(), typename),
                        file=C,
                    )
                    for inst in instances:
                        if not include_ctr(inst):
                            continue
                        print("  %s," % c_str(inst.name), file=C)
                    print("};", file=C)
                    print(
                        "const absl::string_view"
                        " %s::%s_doc[static_cast<int>(%s::COUNT)] = {"
                        % (class_name, typename.lower(), typename),
                        file=C,
                    )
                    for inst in instances:
                        if not include_ctr(inst):
                            continue
                        print("  %s," % c_str(inst.doc), file=C)
                    print("};", file=C)

                print(
                    "%s::%s() : %s {}"
                    % (
                        class_name,
                        class_name,
                        ",".join(
                            "%s{0}" % ctr.name
                            for ctr in self._inst_map["Counter"]
                            if include_ctr(ctr)
                        ),
                    ),
                    file=C,
                )

                if scope == linked_global_scope:
                    print(
                        "HistogramView %s::histogram(Histogram which) const {"
                        % class_name,
                        file=C,
                    )
                    print("  switch (which) {", file=C)
                    print(
                        "    default: GPR_UNREACHABLE_CODE(return HistogramView());",
                        file=C,
                    )
                    for inst in self._inst_map["Histogram"]:
                        if not include_ctr(inst):
                            continue
                        print(
                            "    case Histogram::k%s:"
                            % snake_to_pascal(inst.name),
                            file=C,
                        )
                        shape = histogram_shape(
                            inst, scope == linked_global_scope
                        )
                        print(
                            "      return HistogramView{&Histogram_%s::BucketFor,"
                            " kStatsTable%d, %d, %s.buckets()};"
                            % (
                                shape_signature(shape),
                                histo_bucket_boundaries[shape],
                                shape.buckets,
                                inst.name,
                            ),
                            file=C,
                        )
                    print("  }", file=C)
                    print("}", file=C)

                    print(
                        "std::unique_ptr<%sStats> %sStatsCollector::Collect()"
                        " const {"
                        % (snake_to_pascal(scope), snake_to_pascal(scope)),
                        file=C,
                    )
                    print(
                        "  auto result = std::make_unique<%sStats>();"
                        % snake_to_pascal(scope),
                        file=C,
                    )
                    print("  for (const auto& data : data_) {", file=C)
                    for ctr in self._inst_map["Counter"]:
                        if (
                            ctr.scope != scope
                            and self._linked_global_scopes[ctr.scope] != scope
                        ):
                            continue
                        print(
                            "    result->%s +="
                            " data.%s.load(std::memory_order_relaxed);"
                            % (ctr.name, ctr.name),
                            file=C,
                        )
                    for h in self._inst_map["Histogram"]:
                        if (
                            h.scope != scope
                            and self._linked_global_scopes[h.scope] != scope
                        ):
                            continue
                        print(
                            "    data.%s.Collect(&result->%s);"
                            % (h.name, h.name),
                            file=C,
                        )
                    print("  }", file=C)
                    print("  return result;", file=C)
                    print("}", file=C)

                    print(
                        (
                            "std::unique_ptr<%sStats> %sStats::Diff(const"
                            " %sStats& other) const {"
                        )
                        % (
                            snake_to_pascal(scope),
                            snake_to_pascal(scope),
                            snake_to_pascal(scope),
                        ),
                        file=C,
                    )
                    print(
                        "  auto result = std::make_unique<%sStats>();"
                        % snake_to_pascal(scope),
                        file=C,
                    )
                    for ctr in self._inst_map["Counter"]:
                        if (
                            ctr.scope != scope
                            and self._linked_global_scopes[ctr.scope] != scope
                        ):
                            continue
                        print(
                            "  result->%s = %s - other.%s;"
                            % (ctr.name, ctr.name, ctr.name),
                            file=C,
                        )
                    for h in self._inst_map["Histogram"]:
                        if (
                            h.scope != scope
                            and self._linked_global_scopes[h.scope] != scope
                        ):
                            continue
                        print(
                            "  result->%s = %s - other.%s;"
                            % (h.name, h.name, h.name),
                            file=C,
                        )
                    print("  return result;", file=C)
                    print("}", file=C)

            print("}", file=C)
