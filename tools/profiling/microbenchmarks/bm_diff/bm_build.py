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

""" Python utility to build opt and counters benchmarks """

import bm_constants

import argparse
import subprocess
import multiprocessing
import os
import shutil


def _args():
  argp = argparse.ArgumentParser(description='Builds microbenchmarks')
  argp.add_argument(
    '-b',
    '--benchmarks',
    nargs='+',
    choices=bm_constants._AVAILABLE_BENCHMARK_TESTS,
    default=bm_constants._AVAILABLE_BENCHMARK_TESTS,
    help='Which benchmarks to build')
  argp.add_argument(
    '-j',
    '--jobs',
    type=int,
    default=multiprocessing.cpu_count(),
    help='How many CPUs to dedicate to this task')
  argp.add_argument(
    '-n',
    '--name',
    type=str,
    help='Unique name of this build. To be used as a handle to pass to the other bm* scripts'
  )
  argp.add_argument('--counters', dest='counters', action='store_true')
  argp.add_argument('--no-counters', dest='counters', action='store_false')
  argp.set_defaults(counters=True)
  args = argp.parse_args()
  assert args.name
  return args


def _make_cmd(cfg, benchmarks, jobs):
  return ['make'] + benchmarks + ['CONFIG=%s' % cfg, '-j', '%d' % jobs]


def build(name, benchmarks, jobs, counters):
  shutil.rmtree('bm_diff_%s' % name, ignore_errors=True)
  subprocess.check_call(['git', 'submodule', 'update'])
  try:
    subprocess.check_call(_make_cmd('opt', benchmarks, jobs))
    if counters:
      subprocess.check_call(_make_cmd('counters', benchmarks, jobs))
  except subprocess.CalledProcessError, e:
    subprocess.check_call(['make', 'clean'])
    subprocess.check_call(_make_cmd('opt', benchmarks, jobs))
    if counters:
      subprocess.check_call(_make_cmd('counters', benchmarks, jobs))
  os.rename(
    'bins',
    'bm_diff_%s' % name,)


if __name__ == '__main__':
  args = _args()
  build(args.name, args.benchmarks, args.jobs, args.counters)
