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

### Python utility to build opt and counters benchmarks """

import bm_constants

import argparse
import subprocess
import multiprocessing
import os
import shutil

def _args():
  argp = argparse.ArgumentParser(description='Builds microbenchmarks')
  argp.add_argument('-b', '--benchmarks', nargs='+', choices=bm_constants._AVAILABLE_BENCHMARK_TESTS, default=bm_constants._AVAILABLE_BENCHMARK_TESTS)
  argp.add_argument('-j', '--jobs', type=int, default=multiprocessing.cpu_count())
  argp.add_argument('-n', '--name', type=str, help='Unique name of this build')
  return argp.parse_args()

def _make_cmd(cfg, jobs, benchmarks):
  return ['make'] + benchmarks + [
      'CONFIG=%s' % cfg, '-j', '%d' % jobs]

def build(name, jobs, benchmarks):
  shutil.rmtree('bm_diff_%s' % name, ignore_errors=True)
  subprocess.check_call(['git', 'submodule', 'update'])
  try:
    subprocess.check_call(_make_cmd('opt', jobs, benchmarks))
    subprocess.check_call(_make_cmd('counters', jobs, benchmarks))
  except subprocess.CalledProcessError, e:
    subprocess.check_call(['make', 'clean'])
    subprocess.check_call(_make_cmd('opt', jobs, benchmarks))
    subprocess.check_call(_make_cmd('counters', jobs, benchmarks))
  os.rename('bins', 'bm_diff_%s' % name, )

if __name__ == '__main__':
  args = _args()
  build(args.name, args.jobs, args.benchmarks)


