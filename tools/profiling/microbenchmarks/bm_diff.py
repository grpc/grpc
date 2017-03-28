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

import sys
import json
import bm_json
import tabulate
import argparse
import scipy

def changed_ratio(n, o):
  if float(o) <= .0001: o = 0
  if float(n) <= .0001: n = 0
  if o == 0 and n == 0: return 0
  if o == 0: return 100
  return (float(n)-float(o))/float(o)

def min_change(pct):
  return lambda n, o: abs(changed_ratio(n,o)) > pct/100.0

_INTERESTING = [
  'cpu_time',
  'real_time',
  'locks_per_iteration',
  'allocs_per_iteration',
  'writes_per_iteration',
  'atm_cas_per_iteration',
  'atm_add_per_iteration',
]

_AVAILABLE_BENCHMARK_TESTS = ['bm_fullstack_unary_ping_pong',
                              'bm_fullstack_streaming_ping_pong',
                              'bm_fullstack_streaming_pump',
                              'bm_closure',
                              'bm_cq',
                              'bm_call_create',
                              'bm_error',
                              'bm_chttp2_hpack',
                              'bm_chttp2_transport',
                              'bm_pollset',
                              'bm_metadata',
                              'bm_fullstack_trickle']

argp = argparse.ArgumentParser(description='Perform diff on microbenchmarks')
argp.add_argument('-t', '--track',
                  choices=sorted(_INTERESTING),
                  nargs='+',
                  default=sorted(_INTERESTING),
                  help='Which metrics to track')
argp.add_argument('-b', '--benchmarks', nargs='+', choices=_AVAILABLE_BENCHMARK_TESTS, default=['bm_error'])
argp.add_argument('-d', '--diff_base', type=str)
argp.add_argument('-r', '--repetitions', type=int, default=5)
argp.add_argument('-p', '--p_threshold', type=float, default=0.05)
args = argp.parse_args()

assert args.diff_base

def collect1(bm, cfg, ver):
  subprocess.check_call(['make', 'clean'])
  subprocess.check_call(
      ['make', bm_name,
       'CONFIG=%s' % cfg, '-j', '%d' % multiprocessing.cpu_count()])
  cmd = ['bins/%s/%s' % (cfg, bm),
         '--benchmark_out=%s.%s.%s.json' % (bm, cfg, ver),
         '--benchmark_out_format=json',
         '--benchmark_repetitions=%d' % (args.repetitions)
         ]
  subprocess.check_call(cmd)

for bm in args.benchmarks:
  collect1(bm, 'opt', 'new')
  collect1(bm, 'counters', 'new')

git_comment = 'Performance differences between this PR and %s\\n' % args.diff_perf

where_am_i = subprocess.check_output(['git', 'rev-parse', '--abbrev-ref', 'HEAD']).strip()
subprocess.check_call(['git', 'checkout', args.diff_base])

try:
  comparables = []
  for bm in args.benchmarks:
    try:
      collect1(bm, 'opt', 'old')
      collect1(bm, 'counters', 'old')
      comparables.append(bm_name)
    except subprocess.CalledProcessError, e:
      pass
finally:
  subprocess.check_call(['git', 'checkout', where_am_i])


class Benchmark:

  def __init__(self):
    self.samples = {
      True: collections.defaultdict(list),
      False: collections.defaultdict(list)
    }
    self.final = {}

  def add_sample(self, data, new):
    for f in _INTERESTING:
      if f in data:
        self.samples[new][f].append(data[f])

  def process(self):
    for f in _INTERESTING:
      new = self.samples[True][f]
      old = self.samples[False][f]
      if not new or not old: continue
      p = scipy.stats.ttest_ind(new, old)
      if p < args.p_threshold:
        self.final[f] = avg(new) - avg(old)
    return self.final.keys()

  def row(self, flds):
    return [self.final[f] if f in self.final else '' for f in flds]


benchmarks = collections.defaultdict(Benchmark)

for bm in comparables:
  with open('%s.counters.new.json' % bm) as f:
    js_new_ctr = json.loads(f.read())
  with open('%s.opt.new.json' % bm) as f:
    js_new_opt = json.loads(f.read())
  with open('%s.counters.old.json' % bm) as f:
    js_old_ctr = json.loads(f.read())
  with open('%s.opt.old.json' % bm) as f:
    js_old_opt = json.loads(f.read())

  for row in bm_json.expand_json(js_new_ctr, js_new_opt):
    name = row['cpp_name']
    if name.endswith('_mean') or nme.endswith('_stddev'): continue
    benchmarks[name].add_sample(row, True)
  for row in bm_json.expand_json(js_old_ctr, js_old_opt):
    name = row['cpp_name']
    if name.endswith('_mean') or nme.endswith('_stddev'): continue
    benchmarks[name].add_sample(row, False)

really_interesting = set()
for bm in benchmarks:
  really_interesting.update(bm.process())
fields = [f for f in _INTERESTING if f in really_interesting]

headers = ['Benchmark'] + fields
rows = []
for name in sorted(benchmarks.keys()):
  rows.append([name] + benchmarks[name].row(fields))
print tabulate.tabulate(rows, headers=headers, floatfmt='+.2f')
