#!/usr/bin/env python2.7
#
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

""" Computes the diff between two qps runs and outputs significant results """

import argparse
import json
import multiprocessing
import os
import qps_scenarios
import shutil
import subprocess
import sys
import tabulate

sys.path.append(
  os.path.join(
    os.path.dirname(sys.argv[0]), '..', 'microbenchmarks', 'bm_diff'))
import bm_speedup

sys.path.append(
  os.path.join(
    os.path.dirname(sys.argv[0]), '..', '..', 'run_tests', 'python_utils'))
import comment_on_pr


def _args():
  argp = argparse.ArgumentParser(
    description='Perform diff on QPS Driver')
  argp.add_argument(
    '-d',
    '--diff_base',
    type=str,
    help='Commit or branch to compare the current one to')
  argp.add_argument(
    '-l',
    '--loops',
    type=int,
    default=4,
    help='Number of loops for each benchmark. More loops cuts down on noise'
  )
  argp.add_argument(
    '-j',
    '--jobs',
    type=int,
    default=multiprocessing.cpu_count(),
    help='Number of CPUs to use')
  args = argp.parse_args()
  assert args.diff_base, "diff_base must be set"
  return args


def _make_cmd(jobs):
  return ['make', '-j', '%d' % jobs, 'qps_json_driver', 'qps_worker']


def build(name, jobs):
  shutil.rmtree('qps_diff_%s' % name, ignore_errors=True)
  subprocess.check_call(['git', 'submodule', 'update'])
  try:
    subprocess.check_call(_make_cmd(jobs))
  except subprocess.CalledProcessError, e:
    subprocess.check_call(['make', 'clean'])
    subprocess.check_call(_make_cmd(jobs))
  os.rename('bins', 'qps_diff_%s' % name)


def _run_cmd(name, scenario, fname):
  return ['qps_diff_%s/opt/qps_json_driver' % name, '--scenarios_json', scenario, '--json_file_out', fname]


def run(name, scenarios, loops):
  for sn in scenarios:
    for i in range(0, loops):
      fname = "%s.%s.%d.json" % (sn, name, i)
      subprocess.check_call(_run_cmd(name, scenarios[sn], fname))


def _load_qps(fname):
  try:
    with open(fname) as f:
      return json.loads(f.read())['qps']
  except IOError, e:
    print("IOError occurred reading file: %s" % fname)
    return None
  except ValueError, e:
    print("ValueError occurred reading file: %s" % fname)
    return None


def _median(ary):
  assert (len(ary))
  ary = sorted(ary)
  n = len(ary)
  if n % 2 == 0:
    return (ary[(n - 1) / 2] + ary[(n - 1) / 2 + 1]) / 2.0
  else:
    return ary[n / 2]


def diff(scenarios, loops, old, new):
  old_data = {}
  new_data = {}

  # collect data
  for sn in scenarios:
    old_data[sn] = []
    new_data[sn] = []
    for i in range(loops):
      old_data[sn].append(_load_qps("%s.%s.%d.json" % (sn, old, i)))
      new_data[sn].append(_load_qps("%s.%s.%d.json" % (sn, new, i)))

  # crunch data
  headers = ['Benchmark', 'qps']
  rows = []
  for sn in scenarios:
    mdn_diff = abs(_median(new_data[sn]) - _median(old_data[sn]))
    print('%s: %s=%r %s=%r mdn_diff=%r' % (sn, new, new_data[sn], old, old_data[sn], mdn_diff))
    s = bm_speedup.speedup(new_data[sn], old_data[sn], 10e-5)
    if abs(s) > 3 and mdn_diff > 0.5:
      rows.append([sn, '%+d%%' % s])

  if rows:
    return tabulate.tabulate(rows, headers=headers, floatfmt='+.2f')
  else:
    return None


def main(args):
  build('new', args.jobs)

  if args.diff_base:
    where_am_i = subprocess.check_output(
      ['git', 'rev-parse', '--abbrev-ref', 'HEAD']).strip()
    subprocess.check_call(['git', 'checkout', args.diff_base])
    try:
      build('old', args.jobs)
    finally:
      subprocess.check_call(['git', 'checkout', where_am_i])
      subprocess.check_call(['git', 'submodule', 'update'])

  run('new', qps_scenarios._SCENARIOS, args.loops)
  run('old', qps_scenarios._SCENARIOS, args.loops)

  diff_output = diff(qps_scenarios._SCENARIOS, args.loops, 'old', 'new')

  if diff_output:
    text = '[qps] Performance differences noted:\n%s' % diff_output
  else:
    text = '[qps] No significant performance differences'
  print('%s' % text)
  comment_on_pr.comment_on_pr('```\n%s\n```' % text)


if __name__ == '__main__':
  args = _args()
  main(args)
