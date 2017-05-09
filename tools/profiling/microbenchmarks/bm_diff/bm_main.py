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

""" Runs the entire bm_*.py pipeline, and possible comments on the PR """

import bm_constants
import bm_build
import bm_run
import bm_diff

import sys
import os
import argparse
import multiprocessing
import subprocess

sys.path.append(os.path.join(os.path.dirname(sys.argv[0]), '..', '..', 'run_tests', 'python_utils'))
import comment_on_pr

def _args():
  argp = argparse.ArgumentParser(description='Perform diff on microbenchmarks')
  argp.add_argument('-t', '--track',
                    choices=sorted(bm_constants._INTERESTING),
                    nargs='+',
                    default=sorted(bm_constants._INTERESTING),
                    help='Which metrics to track')
  argp.add_argument('-b', '--benchmarks', nargs='+', choices=bm_constants._AVAILABLE_BENCHMARK_TESTS, default=bm_constants._AVAILABLE_BENCHMARK_TESTS)
  argp.add_argument('-d', '--diff_base', type=str)
  argp.add_argument('-r', '--repetitions', type=int, default=1)
  argp.add_argument('-l', '--loops', type=int, default=20)
  argp.add_argument('-j', '--jobs', type=int, default=multiprocessing.cpu_count())
  args = argp.parse_args()
  assert args.diff_base
  return args


def eintr_be_gone(fn):
  """Run fn until it doesn't stop because of EINTR"""
  def inner(*args):
    while True:
      try:
        return fn(*args)
      except IOError, e:
        if e.errno != errno.EINTR:
          raise
  return inner

def main(args):

  bm_build.build('new', args.benchmarks, args.jobs)

  where_am_i = subprocess.check_output(['git', 'rev-parse', '--abbrev-ref', 'HEAD']).strip()
  subprocess.check_call(['git', 'checkout', args.diff_base])
  try:
    bm_build.build('old', args.benchmarks, args.jobs)
  finally:
    subprocess.check_call(['git', 'checkout', where_am_i])
    subprocess.check_call(['git', 'submodule', 'update'])

  bm_run.run('new', args.benchmarks, args.jobs, args.loops, args.repetitions)
  bm_run.run('old', args.benchmarks, args.jobs, args.loops, args.repetitions)

  diff = bm_diff.diff(args.benchmarks, args.loops, args.track, 'old', 'new')
  if diff:
    text = 'Performance differences noted:\n' + diff
  else:
    text = 'No significant performance differences'
  print text
  comment_on_pr.comment_on_pr('```\n%s\n```' % text)

if __name__ == '__main__':
  args = _args()
  main(args)
