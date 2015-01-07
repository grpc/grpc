#!/usr/bin/python
"""Run tests in parallel."""

import argparse
import glob
import itertools
import multiprocessing
import sys

import jobset

# flags required for make for each configuration
_CONFIGS = ['dbg', 'opt', 'tsan', 'msan', 'asan']

# parse command line
argp = argparse.ArgumentParser(description='Run grpc tests.')
argp.add_argument('-c', '--config',
                  choices=['all'] + _CONFIGS,
                  nargs='+',
                  default=['all'])
argp.add_argument('-t', '--test-filter', nargs='*', default=['*'])
argp.add_argument('-n', '--runs_per_test', default=1, type=int)
args = argp.parse_args()

# grab config
configs = [cfg
           for cfg in itertools.chain.from_iterable(
               _CONFIGS if x == 'all' else [x]
               for x in args.config)]
filters = args.test_filter
runs_per_test = args.runs_per_test

# build latest, sharing cpu between the various makes
if not jobset.run(
    ['make',
     '-j', '%d' % max(multiprocessing.cpu_count() / len(configs), 1),
     'buildtests_c',
     'CONFIG=%s' % cfg]
    for cfg in configs):
  sys.exit(1)

# run all the tests
jobset.run([x]
           for x in itertools.chain.from_iterable(
               itertools.chain.from_iterable(itertools.repeat(
                   glob.glob('bins/%s/%s_test' % (config, filt)),
                   runs_per_test))
               for config in configs
               for filt in filters))
