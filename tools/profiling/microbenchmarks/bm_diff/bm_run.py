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

""" Python utility to run opt and counters benchmarks and save json output """

import bm_constants

import argparse
import subprocess
import multiprocessing
import random
import itertools
import sys
import os

sys.path.append(
  os.path.join(
    os.path.dirname(sys.argv[0]), '..', '..', '..', 'run_tests',
    'python_utils'))
import jobset


def _args():
  argp = argparse.ArgumentParser(description='Runs microbenchmarks')
  argp.add_argument(
    '-b',
    '--benchmarks',
    nargs='+',
    choices=bm_constants._AVAILABLE_BENCHMARK_TESTS,
    default=bm_constants._AVAILABLE_BENCHMARK_TESTS,
    help='Benchmarks to run')
  argp.add_argument(
    '-j',
    '--jobs',
    type=int,
    default=multiprocessing.cpu_count(),
    help='Number of CPUs to use')
  argp.add_argument(
    '-n',
    '--name',
    type=str,
    help='Unique name of the build to run. Needs to match the handle passed to bm_build.py'
  )
  argp.add_argument(
    '-r',
    '--repetitions',
    type=int,
    default=1,
    help='Number of repetitions to pass to the benchmarks')
  argp.add_argument(
    '-l',
    '--loops',
    type=int,
    default=20,
    help='Number of times to loops the benchmarks. More loops cuts down on noise'
  )
  argp.add_argument('--counters', dest='counters', action='store_true')
  argp.add_argument('--no-counters', dest='counters', action='store_false')
  argp.set_defaults(counters=True)
  args = argp.parse_args()
  assert args.name
  if args.loops < 3:
    print "WARNING: This run will likely be noisy. Increase loops to at least 3."
  return args


def _collect_bm_data(bm, cfg, name, reps, idx, loops):
  jobs_list = []
  for line in subprocess.check_output(
    ['bm_diff_%s/%s/%s' % (name, cfg, bm),
     '--benchmark_list_tests']).splitlines():
    stripped_line = line.strip().replace("/", "_").replace(
      "<", "_").replace(">", "_").replace(", ", "_")
    cmd = [
      'bm_diff_%s/%s/%s' % (name, cfg, bm), '--benchmark_filter=^%s$' %
      line, '--benchmark_out=%s.%s.%s.%s.%d.json' %
      (bm, stripped_line, cfg, name, idx), '--benchmark_out_format=json',
      '--benchmark_repetitions=%d' % (reps)
    ]
    jobs_list.append(
      jobset.JobSpec(
        cmd,
        shortname='%s %s %s %s %d/%d' % (bm, line, cfg, name, idx + 1,
                         loops),
        verbose_success=True,
        timeout_seconds=60 * 60)) # one hour
  return jobs_list


def run(name, benchmarks, jobs, loops, reps, counters):
  jobs_list = []
  for loop in range(0, loops):
    for bm in benchmarks:
      jobs_list += _collect_bm_data(bm, 'opt', name, reps, loop, loops)
      if counters:
        jobs_list += _collect_bm_data(bm, 'counters', name, reps, loop,
                        loops)
  random.shuffle(jobs_list, random.SystemRandom().random)
  jobset.run(jobs_list, maxjobs=jobs)


if __name__ == '__main__':
  args = _args()
  run(args.name, args.benchmarks, args.jobs, args.loops, args.repetitions, args.counters)
