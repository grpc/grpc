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
from scipy import stats
import subprocess
import multiprocessing
import collections
import pipes
import os
sys.path.append(os.path.join(os.path.dirname(sys.argv[0]), '..', '..', 'run_tests', 'python_utils'))
import comment_on_pr

def changed_ratio(n, o):
  if float(o) <= .0001: o = 0
  if float(n) <= .0001: n = 0
  if o == 0 and n == 0: return 0
  if o == 0: return 100
  return (float(n)-float(o))/float(o)

def median(ary):
  ary = sorted(ary)
  n = len(ary)
  if n%2 == 0:
    return (ary[n/2] + ary[n/2+1]) / 2.0
  else:
    return ary[n/2]

def min_change(pct):
  return lambda n, o: abs(changed_ratio(n,o)) > pct/100.0

nanos = {
  'abs_diff': 5,
  'pct_diff': 10,
}
counter = {
  'abs_diff': 0.5,
  'pct_diff': 10,
}

_INTERESTING = {
  'cpu_time': nanos,
  'real_time': nanos,
  'locks_per_iteration': counter,
  'allocs_per_iteration': counter,
  'writes_per_iteration': counter,
  'atm_cas_per_iteration': counter,
  'atm_add_per_iteration': counter,
}


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
                  choices=sorted(_INTERESTING.keys()),
                  nargs='+',
                  default=sorted(_INTERESTING.keys()),
                  help='Which metrics to track')
argp.add_argument('-b', '--benchmarks', nargs='+', choices=_AVAILABLE_BENCHMARK_TESTS, default=['bm_cq'])
argp.add_argument('-d', '--diff_base', type=str)
argp.add_argument('-r', '--repetitions', type=int, default=4)
argp.add_argument('-p', '--p_threshold', type=float, default=0.01)
args = argp.parse_args()

assert args.diff_base

def avg(lst):
  sum = 0.0
  n = 0.0
  for el in lst:
    sum += el
    n += 1
  return sum / n

def make_cmd(cfg):
  return ['make'] + args.benchmarks + [
      'CONFIG=%s' % cfg, '-j', '%d' % multiprocessing.cpu_count()]

def build():
  subprocess.check_call(['git', 'submodule', 'update'])
  try:
    subprocess.check_call(make_cmd('opt'))
    subprocess.check_call(make_cmd('counters'))
  except subprocess.CalledProcessError, e:
    subprocess.check_call(['make', 'clean'])
    subprocess.check_call(make_cmd('opt'))
    subprocess.check_call(make_cmd('counters'))

def collect1(bm, cfg, ver):
  cmd = ['bins/%s/%s' % (cfg, bm),
         '--benchmark_out=%s.%s.%s.json' % (bm, cfg, ver),
         '--benchmark_out_format=json',
         '--benchmark_repetitions=%d' % (args.repetitions)
         ]
  print cmd
  subprocess.check_call(cmd)

build()
for bm in args.benchmarks:
  collect1(bm, 'opt', 'new')
  collect1(bm, 'counters', 'new')

where_am_i = subprocess.check_output(['git', 'rev-parse', '--abbrev-ref', 'HEAD']).strip()
subprocess.check_call(['git', 'checkout', args.diff_base])

try:
  build()
  comparables = []
  for bm in args.benchmarks:
    try:
      collect1(bm, 'opt', 'old')
      collect1(bm, 'counters', 'old')
      comparables.append(bm)
    except subprocess.CalledProcessError, e:
      pass
finally:
  subprocess.check_call(['git', 'checkout', where_am_i])
  subprocess.check_call(['git', 'submodule', 'update'])


class Benchmark:

  def __init__(self):
    self.samples = {
      True: collections.defaultdict(list),
      False: collections.defaultdict(list)
    }
    self.final = {}

  def add_sample(self, data, new):
    for f in args.track:
      if f in data:
        self.samples[new][f].append(float(data[f]))

  def process(self):
    for f in sorted(args.track):
      new = self.samples[True][f]
      old = self.samples[False][f]
      if not new or not old: continue
      p = stats.ttest_ind(new, old)[1]
      new_mdn = median(new)
      old_mdn = median(old)
      delta = new_mdn - old_mdn
      ratio = changed_ratio(new_mdn, old_mdn)
      print '%s: new=%r old=%r new_mdn=%f old_mdn=%f delta=%f(%f:%f) ratio=%f(%f:%f) p=%f' % (
      f, new, old, new_mdn, old_mdn, delta, abs(delta), _INTERESTING[f]['abs_diff'], ratio, abs(ratio), _INTERESTING[f]['pct_diff']/100.0, p
      )
      if p < args.p_threshold and abs(delta) > _INTERESTING[f]['abs_diff'] and abs(ratio) > _INTERESTING[f]['pct_diff']/100.0:
        self.final[f] = delta
    return self.final.keys()

  def skip(self):
    return not self.final

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
    print row
    name = row['cpp_name']
    if name.endswith('_mean') or name.endswith('_stddev'): continue
    benchmarks[name].add_sample(row, True)
  for row in bm_json.expand_json(js_old_ctr, js_old_opt):
    print row
    name = row['cpp_name']
    if name.endswith('_mean') or name.endswith('_stddev'): continue
    benchmarks[name].add_sample(row, False)

really_interesting = set()
for name, bm in benchmarks.items():
  print name
  really_interesting.update(bm.process())
fields = [f for f in args.track if f in really_interesting]

headers = ['Benchmark'] + fields
rows = []
for name in sorted(benchmarks.keys()):
  if benchmarks[name].skip(): continue
  rows.append([name] + benchmarks[name].row(fields))
if rows:
  text = 'Performance differences noted:\n' + tabulate.tabulate(rows, headers=headers, floatfmt='+.2f')
else:
  text = 'No significant performance differences'
comment_on_pr.comment_on_pr('```\n%s\n```' % text)
print text
