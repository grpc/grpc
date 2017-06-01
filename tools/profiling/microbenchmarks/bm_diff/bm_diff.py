#!/usr/bin/env python2.7
# Copyright 2017, Google Inc.
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
    ary = sorted(ary)
    n = len(ary)
    if n % 2 == 0:
        return (ary[n / 2] + ary[n / 2 + 1]) / 2.0
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
        help='Number of times to loops the benchmarks. Must match what was passed to bm_run.py'
    )
    argp.add_argument('-n', '--new', type=str, help='New benchmark name')
    argp.add_argument('-o', '--old', type=str, help='Old benchmark name')
    argp.add_argument(
        '-v', '--verbose', type=bool, help='print details of before/after')
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
            s = bm_speedup.speedup(new, old)
            if abs(s) > 3 and mdn_diff > 0.5:
                self.final[f] = '%+d%%' % s
        return self.final.keys()

    def skip(self):
        return not self.final

    def row(self, flds):
        return [self.final[f] if f in self.final else '' for f in flds]


def _read_json(filename):
    try:
        with open(filename) as f:
            return json.loads(f.read())
    except ValueError, e:
        return None


def diff(bms, loops, track, old, new):
    benchmarks = collections.defaultdict(Benchmark)

    for bm in bms:
        for loop in range(0, loops):
            for line in subprocess.check_output(['bm_diff_%s/opt/%s' % (old, bm),
                                       '--benchmark_list_tests']).splitlines():
                stripped_line = line.strip().replace("/","_").replace("<","_").replace(">","_")
                js_new_ctr = _read_json('%s.%s.counters.%s.%d.json' % (bm, stripped_line, new, loop))
                js_new_opt = _read_json('%s.%s.opt.%s.%d.json' % (bm, stripped_line, new, loop))
                js_old_ctr = _read_json('%s.%s.counters.%s.%d.json' % (bm, stripped_line, old, loop))
                js_old_opt = _read_json('%s.%s.opt.%s.%d.json' % (bm, stripped_line, old, loop))

                if js_new_ctr:
                    for row in bm_json.expand_json(js_new_ctr, js_new_opt):
                        name = row['cpp_name']
                        if name.endswith('_mean') or name.endswith('_stddev'):
                            continue
                        benchmarks[name].add_sample(track, row, True)
                if js_old_ctr:
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
    if rows:
        return tabulate.tabulate(rows, headers=headers, floatfmt='+.2f')
    else:
        return None


if __name__ == '__main__':
    args = _args()
    print diff(args.benchmarks, args.loops, args.track, args.old, args.new)
