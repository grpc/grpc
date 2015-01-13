#!/usr/bin/python
"""Run tests in parallel."""

import argparse
import glob
import itertools
import multiprocessing
import sys
import time

import jobset
import watch_dirs

# flags required for make for each configuration
_CONFIGS = ['dbg', 'opt', 'tsan', 'msan', 'asan']
_MAKE_TEST_TARGETS = ['buildtests_c', 'buildtests_cxx']

# parse command line
argp = argparse.ArgumentParser(description='Run grpc tests.')
argp.add_argument('-c', '--config',
                  choices=['all'] + _CONFIGS,
                  nargs='+',
                  default=['all'])
argp.add_argument('-t', '--test-filter', nargs='*', default=['*'])
argp.add_argument('-n', '--runs_per_test', default=1, type=int)
argp.add_argument('-f', '--forever',
                  default=False,
                  action='store_const',
                  const=True)
args = argp.parse_args()

# grab config
configs = [cfg
           for cfg in itertools.chain.from_iterable(
               _CONFIGS if x == 'all' else [x]
               for x in args.config)]
filters = args.test_filter
runs_per_test = args.runs_per_test
forever = args.forever


def _build_and_run(check_cancelled):
  """Do one pass of building & running tests."""
  # build latest, sharing cpu between the various makes
  if not jobset.run(
      (['make',
        '-j', '%d' % (multiprocessing.cpu_count() + 1),
        target,
        'CONFIG=%s' % cfg]
       for cfg in configs
       for target in _MAKE_TEST_TARGETS),
      check_cancelled, maxjobs=1):
    sys.exit(1)

  # run all the tests
  jobset.run(([x]
              for x in itertools.chain.from_iterable(
                  itertools.chain.from_iterable(itertools.repeat(
                      glob.glob('bins/%s/%s_test' % (config, filt)),
                      runs_per_test))
                  for config in configs
                  for filt in filters)), check_cancelled)


if forever:
  while True:
    dw = watch_dirs.DirWatcher(['src', 'include', 'test'])
    initial_time = dw.most_recent_change()
    have_files_changed = lambda: dw.most_recent_change() != initial_time
    _build_and_run(have_files_changed)
    while not have_files_changed():
      time.sleep(1)
else:
  _build_and_run(lambda: False)

