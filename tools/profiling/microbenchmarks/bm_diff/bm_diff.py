#!/usr/bin/env python2.7
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

import bm_constants
import bm_speedup

import sys
import os

sys.path.append(os.path.join(os.path.dirname(sys.argv[0]), '..'))
import bm_json

import json
import tabulate
import argparse
import collections
import subprocess

verbose = False


def _median(ary):
    assert (len(ary))
    ary = sorted(ary)
    n = len(ary)
    if n % 2 == 0:
        return (ary[(n - 1) / 2] + ary[(n - 1) / 2 + 1]) / 2.0
    else:
        return ary[n / 2]


def _args():
    argp = argparse.ArgumentParser(
        description='Perform diff on microbenchmarks')
    argp.add_argument(
        '-t',
        '--track',
        choices=sorted(bm_constants._INTERESTING),
        nargs='+',
        default=sorted(bm_constants._INTERESTING),
        help='Which metrics to track')
    argp.add_argument(
        '-b',
        '--benchmarks',
        nargs='+',
        choices=bm_constants._AVAILABLE_BENCHMARK_TESTS,
        default=bm_constants._AVAILABLE_BENCHMARK_TESTS,
        help='Which benchmarks to run')
    argp.add_argument(
        '-l',
        '--loops',
        type=int,
        default=20,
        help=
        'Number of times to loops the benchmarks. Must match what was passed to bm_run.py'
    )
    argp.add_argument(
        '-r',
        '--regex',
        type=str,
        default="",
        help='Regex to filter benchmarks run')
    argp.add_argument('--counters', dest='counters', action='store_true')
    argp.add_argument('--no-counters', dest='counters', action='store_false')
    argp.set_defaults(counters=True)
    argp.add_argument('-n', '--new', type=str, help='New benchmark name')
    argp.add_argument('-o', '--old', type=str, help='Old benchmark name')
    argp.add_argument(
        '-v', '--verbose', type=bool, help='Print details of before/after')
    args = argp.parse_args()
    global verbose
    if args.verbose: verbose = True
    assert args.new
    assert args.old
    return args


def _maybe_print(str):
    if verbose: print str


class Benchmark:

    def __init__(self):
        self.samples = {
            True: collections.defaultdict(list),
            False: collections.defaultdict(list)
        }
        self.final = {}

    def add_sample(self, track, data, new):
        for f in track:
            if f in data:
                self.samples[new][f].append(float(data[f]))

    def process(self, track, new_name, old_name):
        for f in sorted(track):
            new = self.samples[True][f]
            old = self.samples[False][f]
            if not new or not old: continue
            mdn_diff = abs(_median(new) - _median(old))
            _maybe_print('%s: %s=%r %s=%r mdn_diff=%r' %
                         (f, new_name, new, old_name, old, mdn_diff))
            s = bm_speedup.speedup(new, old, 1e-5)
            if abs(s) > 3:
                if mdn_diff > 0.5 or 'trickle' in f:
                    self.final[f] = '%+d%%' % s
        return self.final.keys()

    def skip(self):
        return not self.final

    def row(self, flds):
        return [self.final[f] if f in self.final else '' for f in flds]


def _read_json(filename, badjson_files, nonexistant_files):
    stripped = ".".join(filename.split(".")[:-2])
    try:
        with open(filename) as f:
            r = f.read()
            return json.loads(r)
    except IOError, e:
        if stripped in nonexistant_files:
            nonexistant_files[stripped] += 1
        else:
            nonexistant_files[stripped] = 1
        return None
    except ValueError, e:
        print r
        if stripped in badjson_files:
            badjson_files[stripped] += 1
        else:
            badjson_files[stripped] = 1
        return None


def fmt_dict(d):
    return ''.join(["    " + k + ": " + str(d[k]) + "\n" for k in d])


def diff(bms, loops, regex, track, old, new, counters):
    benchmarks = collections.defaultdict(Benchmark)

    badjson_files = {}
    nonexistant_files = {}
    for bm in bms:
        for loop in range(0, loops):
            for line in subprocess.check_output([
                    'bm_diff_%s/opt/%s' % (old, bm), '--benchmark_list_tests',
                    '--benchmark_filter=%s' % regex
            ]).splitlines():
                stripped_line = line.strip().replace("/", "_").replace(
                    "<", "_").replace(">", "_").replace(", ", "_")
                js_new_opt = _read_json('%s.%s.opt.%s.%d.json' %
                                        (bm, stripped_line, new, loop),
                                        badjson_files, nonexistant_files)
                js_old_opt = _read_json('%s.%s.opt.%s.%d.json' %
                                        (bm, stripped_line, old, loop),
                                        badjson_files, nonexistant_files)
                if counters:
                    js_new_ctr = _read_json('%s.%s.counters.%s.%d.json' %
                                            (bm, stripped_line, new, loop),
                                            badjson_files, nonexistant_files)
                    js_old_ctr = _read_json('%s.%s.counters.%s.%d.json' %
                                            (bm, stripped_line, old, loop),
                                            badjson_files, nonexistant_files)
                else:
                    js_new_ctr = None
                    js_old_ctr = None

                for row in bm_json.expand_json(js_new_ctr, js_new_opt):
                    name = row['cpp_name']
                    if name.endswith('_mean') or name.endswith('_stddev'):
                        continue
                    benchmarks[name].add_sample(track, row, True)
                for row in bm_json.expand_json(js_old_ctr, js_old_opt):
                    name = row['cpp_name']
                    if name.endswith('_mean') or name.endswith('_stddev'):
                        continue
                    benchmarks[name].add_sample(track, row, False)

    really_interesting = set()
    for name, bm in benchmarks.items():
        _maybe_print(name)
        really_interesting.update(bm.process(track, new, old))
    fields = [f for f in track if f in really_interesting]

    headers = ['Benchmark'] + fields
    rows = []
    for name in sorted(benchmarks.keys()):
        if benchmarks[name].skip(): continue
        rows.append([name] + benchmarks[name].row(fields))
    note = None
    if len(badjson_files):
        note = 'Corrupt JSON data (indicates timeout or crash): \n%s' % fmt_dict(
            badjson_files)
    if len(nonexistant_files):
        if note:
            note += '\n\nMissing files (indicates new benchmark): \n%s' % fmt_dict(
                nonexistant_files)
        else:
            note = '\n\nMissing files (indicates new benchmark): \n%s' % fmt_dict(
                nonexistant_files)
    if rows:
        return tabulate.tabulate(rows, headers=headers, floatfmt='+.2f'), note
    else:
        return None, note


if __name__ == '__main__':
    args = _args()
    diff, note = diff(args.benchmarks, args.loops, args.regex, args.track,
                      args.old, args.new, args.counters)
    print('%s\n%s' % (note, diff if diff else "No performance differences"))
