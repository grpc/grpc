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


# SimpleConfig: just compile with CONFIG=config, and run the binary to test
class SimpleConfig(object):
  def __init__(self, config):
    self.build_config = config

  def run_command(self, binary):
    return [binary]


# ValgrindConfig: compile with some CONFIG=config, but use valgrind to run
class ValgrindConfig(object):
  def __init__(self, config):
    self.build_config = config

  def run_command(self, binary):
    return ['valgrind', binary]


# different configurations we can run under
_CONFIGS = {
  'dbg': SimpleConfig('dbg'),
  'opt': SimpleConfig('opt'),
  'tsan': SimpleConfig('tsan'),
  'msan': SimpleConfig('msan'),
  'asan': SimpleConfig('asan'),
  'valgrind': ValgrindConfig('dbg'),
  }


_DEFAULT = ['dbg', 'opt']

# parse command line
argp = argparse.ArgumentParser(description='Run grpc tests.')
argp.add_argument('-c', '--config',
                  choices=['all'] + sorted(_CONFIGS.keys()),
                  nargs='+',
                  default=_DEFAULT)
argp.add_argument('-t', '--test-filter', nargs='*', default=['*'])
argp.add_argument('-n', '--runs_per_test', default=1, type=int)
argp.add_argument('-f', '--forever',
                  default=False,
                  action='store_const',
                  const=True)
args = argp.parse_args()

# grab config
run_configs = set(_CONFIGS[cfg]
                  for cfg in itertools.chain.from_iterable(
                      _CONFIGS.iterkeys() if x == 'all' else [x]
                      for x in args.config))
build_configs = set(cfg.build_config for cfg in run_configs)
filters = args.test_filter
runs_per_test = args.runs_per_test
forever = args.forever


def _build_and_run(check_cancelled):
  """Do one pass of building & running tests."""
  # build latest, sharing cpu between the various makes
  if not jobset.run(
      (['make',
        '-j', '%d' % (multiprocessing.cpu_count() + 1),
        'buildtests_c',
        'CONFIG=%s' % cfg]
       for cfg in build_configs), check_cancelled):
    sys.exit(1)

  # run all the tests
  jobset.run((
      config.run_command(x)
      for config in run_configs
      for filt in filters
      for x in itertools.chain.from_iterable(itertools.repeat(
          glob.glob('bins/%s/%s_test' % (
              config.build_config, filt)),
          runs_per_test))), check_cancelled)


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

