#!/usr/bin/env python3
#
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
""" Computes the diff between two bm runs and outputs significant results """

import argparse
import collections
import json
import os
import subprocess
import sys

sys.path.append(os.path.join(os.path.dirname(sys.argv[0]), ".."))

import bm_constants
import bm_json
import bm_speedup
import tabulate

verbose = False


def _median(ary):
    assert len(ary)
    ary = sorted(ary)
    n = len(ary)
    if n % 2 == 0:
        return (ary[(n - 1) // 2] + ary[(n - 1) // 2 + 1]) / 2.0
    else:
        return ary[n // 2]


def _args():
    argp = argparse.ArgumentParser(
        description="Perform diff on microbenchmarks"
    )
    argp.add_argument(
        "-t",
        "--track",
        choices=sorted(bm_constants._INTERESTING),
        nargs="+",
        default=sorted(bm_constants._INTERESTING),
        help="Which metrics to track",
    )
    argp.add_argument(
        "-b",
        "--benchmarks",
        nargs="+",
        choices=bm_constants._AVAILABLE_BENCHMARK_TESTS,
        default=bm_constants._AVAILABLE_BENCHMARK_TESTS,
        help="Which benchmarks to run",
    )
    argp.add_argument(
        "-l",
        "--loops",
        type=int,
        default=20,
        help=(
            "Number of times to loops the benchmarks. Must match what was"
            " passed to bm_run.py"
        ),
    )
    argp.add_argument(
        "-r",
        "--regex",
        type=str,
        default="",
        help="Regex to filter benchmarks run",
    )
    argp.add_argument("-n", "--new", type=str, help="New benchmark name")
    argp.add_argument("-o", "--old", type=str, help="Old benchmark name")
    argp.add_argument(
        "-v", "--verbose", type=bool, help="Print details of before/after"
    )
    args = argp.parse_args()
    global verbose
    if args.verbose:
        verbose = True
    assert args.new
    assert args.old
    return args


def _maybe_print(str):
    if verbose:
        print(str)


class Benchmark:
    def __init__(self):
        self.samples = {
            True: collections.defaultdict(list),
            False: collections.defaultdict(list),
        }
        self.final = {}
        self.speedup = {}

    def add_sample(self, track, data, new):
        for f in track:
            if f in data:
                self.samples[new][f].append(float(data[f]))

    def process(self, track, new_name, old_name):
        for f in sorted(track):
            new = self.samples[True][f]
            old = self.samples[False][f]
            if not new or not old:
                continue
            mdn_diff = abs(_median(new) - _median(old))
            _maybe_print(
                "%s: %s=%r %s=%r mdn_diff=%r"
                % (f, new_name, new, old_name, old, mdn_diff)
            )
            s = bm_speedup.speedup(new, old, 1e-5)
            self.speedup[f] = s
            if abs(s) > 3:
                if mdn_diff > 0.5:
                    self.final[f] = "%+d%%" % s
        return self.final.keys()

    def skip(self):
        return not self.final

    def row(self, flds):
        return [self.final[f] if f in self.final else "" for f in flds]

    def speedup(self, name):
        if name in self.speedup:
            return self.speedup[name]
        return None


def _read_json(filename, badjson_files, nonexistant_files):
    stripped = ".".join(filename.split(".")[:-2])
    try:
        with open(filename) as f:
            r = f.read()
            return json.loads(r)
    except IOError as e:
        if stripped in nonexistant_files:
            nonexistant_files[stripped] += 1
        else:
            nonexistant_files[stripped] = 1
        return None
    except ValueError as e:
        print(r)
        if stripped in badjson_files:
            badjson_files[stripped] += 1
        else:
            badjson_files[stripped] = 1
        return None


def fmt_dict(d):
    return "".join(["    " + k + ": " + str(d[k]) + "\n" for k in d])


def diff(bms, loops, regex, track, old, new):
    benchmarks = collections.defaultdict(Benchmark)

    badjson_files = {}
    nonexistant_files = {}
    for bm in bms:
        for loop in range(0, loops):
            for line in subprocess.check_output(
                [
                    "bm_diff_%s/opt/%s" % (old, bm),
                    "--benchmark_list_tests",
                    "--benchmark_filter=%s" % regex,
                ]
            ).splitlines():
                line = line.decode("UTF-8")
                stripped_line = (
                    line.strip()
                    .replace("/", "_")
                    .replace("<", "_")
                    .replace(">", "_")
                    .replace(", ", "_")
                )
                js_new_opt = _read_json(
                    "%s.%s.opt.%s.%d.json" % (bm, stripped_line, new, loop),
                    badjson_files,
                    nonexistant_files,
                )
                js_old_opt = _read_json(
                    "%s.%s.opt.%s.%d.json" % (bm, stripped_line, old, loop),
                    badjson_files,
                    nonexistant_files,
                )
                if js_new_opt:
                    for row in bm_json.expand_json(js_new_opt):
                        name = row["cpp_name"]
                        if name.endswith("_mean") or name.endswith("_stddev"):
                            continue
                        benchmarks[name].add_sample(track, row, True)
                if js_old_opt:
                    for row in bm_json.expand_json(js_old_opt):
                        name = row["cpp_name"]
                        if name.endswith("_mean") or name.endswith("_stddev"):
                            continue
                        benchmarks[name].add_sample(track, row, False)

    really_interesting = set()
    for name, bm in benchmarks.items():
        _maybe_print(name)
        really_interesting.update(bm.process(track, new, old))
    fields = [f for f in track if f in really_interesting]

    # figure out the significance of the changes... right now we take the 95%-ile
    # benchmark delta %-age, and then apply some hand chosen thresholds
    histogram = []
    _NOISY = ["BM_WellFlushed"]
    for name, bm in benchmarks.items():
        if name in _NOISY:
            print(
                "skipping noisy benchmark '%s' for labelling evaluation" % name
            )
        if bm.skip():
            continue
        d = bm.speedup["cpu_time"]
        if d is None:
            continue
        histogram.append(d)
    histogram.sort()
    print("histogram of speedups: ", histogram)
    if len(histogram) == 0:
        significance = 0
    else:
        delta = histogram[int(len(histogram) * 0.95)]
        mul = 1
        if delta < 0:
            delta = -delta
            mul = -1
        if delta < 2:
            significance = 0
        elif delta < 5:
            significance = 1
        elif delta < 10:
            significance = 2
        else:
            significance = 3
        significance *= mul

    headers = ["Benchmark"] + fields
    rows = []
    for name in sorted(benchmarks.keys()):
        if benchmarks[name].skip():
            continue
        rows.append([name] + benchmarks[name].row(fields))
    note = None
    if len(badjson_files):
        note = (
            "Corrupt JSON data (indicates timeout or crash): \n%s"
            % fmt_dict(badjson_files)
        )
    if len(nonexistant_files):
        if note:
            note += (
                "\n\nMissing files (indicates new benchmark): \n%s"
                % fmt_dict(nonexistant_files)
            )
        else:
            note = (
                "\n\nMissing files (indicates new benchmark): \n%s"
                % fmt_dict(nonexistant_files)
            )
    if rows:
        return (
            tabulate.tabulate(rows, headers=headers, floatfmt="+.2f"),
            note,
            significance,
        )
    else:
        return None, note, 0


if __name__ == "__main__":
    args = _args()
    diff, note = diff(
        args.benchmarks,
        args.loops,
        args.regex,
        args.track,
        args.old,
        args.new,
        args.counters,
    )
    print("%s\n%s" % (note, diff if diff else "No performance differences"))
