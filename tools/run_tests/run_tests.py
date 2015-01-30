#!/usr/bin/python
"""Run tests in parallel."""

import argparse
import glob
import itertools
import json
import multiprocessing
import os
import sys
import time

import jobset
import watch_dirs


# SimpleConfig: just compile with CONFIG=config, and run the binary to test
class SimpleConfig(object):

  def __init__(self, config):
    self.build_config = config
    self.maxjobs = 2 * multiprocessing.cpu_count()
    self.allow_hashing = (config != 'gcov')

  def run_command(self, binary):
    return [binary]


# ValgrindConfig: compile with some CONFIG=config, but use valgrind to run
class ValgrindConfig(object):

  def __init__(self, config, tool):
    self.build_config = config
    self.tool = tool
    self.maxjobs = 2 * multiprocessing.cpu_count()
    self.allow_hashing = False

  def run_command(self, binary):
    return ['valgrind', binary, '--tool=%s' % self.tool]


class CLanguage(object):

  def __init__(self, make_target, test_lang):
    self.allow_hashing = True
    self.make_target = make_target
    with open('tools/run_tests/tests.json') as f:
      js = json.load(f)
      self.binaries = [tgt['name']
                       for tgt in js
                       if tgt['language'] == test_lang]

  def test_binaries(self, config):
    return ['bins/%s/%s' % (config, binary) for binary in self.binaries]

  def make_targets(self):
    return ['buildtests_%s' % self.make_target]

  def build_steps(self):
    return []


class NodeLanguage(object):

  def __init__(self):
    self.allow_hashing = False

  def test_binaries(self, config):
    return ['tools/run_tests/run_node.sh']

  def make_targets(self):
    return ['static_c']

  def build_steps(self):
    return [['tools/run_tests/build_node.sh']]


class PhpLanguage(object):

  def __init__(self):
    self.allow_hashing = False

  def test_binaries(self, config):
    return ['src/php/bin/run_tests.sh']

  def make_targets(self):
    return ['static_c']

  def build_steps(self):
    return [['tools/run_tests/build_php.sh']]


class PythonLanguage(object):

  def __init__(self):
    self.allow_hashing = False

  def test_binaries(self, config):
    return ['tools/run_tests/run_python.sh']

  def make_targets(self):
    return[]

  def build_steps(self):
    return [['tools/run_tests/build_python.sh']]


# different configurations we can run under
_CONFIGS = {
    'dbg': SimpleConfig('dbg'),
    'opt': SimpleConfig('opt'),
    'tsan': SimpleConfig('tsan'),
    'msan': SimpleConfig('msan'),
    'asan': SimpleConfig('asan'),
    'gcov': SimpleConfig('gcov'),
    'memcheck': ValgrindConfig('valgrind', 'memcheck'),
    'helgrind': ValgrindConfig('dbg', 'helgrind')
    }


_DEFAULT = ['dbg', 'opt']
_LANGUAGES = {
    'c++': CLanguage('cxx', 'c++'),
    'c': CLanguage('c', 'c'),
    'node': NodeLanguage(),
    'php': PhpLanguage(),
    'python': PythonLanguage(),
}

# parse command line
argp = argparse.ArgumentParser(description='Run grpc tests.')
argp.add_argument('-c', '--config',
                  choices=['all'] + sorted(_CONFIGS.keys()),
                  nargs='+',
                  default=_DEFAULT)
argp.add_argument('-n', '--runs_per_test', default=1, type=int)
argp.add_argument('-f', '--forever',
                  default=False,
                  action='store_const',
                  const=True)
argp.add_argument('--newline_on_success',
                  default=False,
                  action='store_const',
                  const=True)
argp.add_argument('-l', '--language',
                  choices=sorted(_LANGUAGES.keys()),
                  nargs='+',
                  default=sorted(_LANGUAGES.keys()))
args = argp.parse_args()

# grab config
run_configs = set(_CONFIGS[cfg]
                  for cfg in itertools.chain.from_iterable(
                      _CONFIGS.iterkeys() if x == 'all' else [x]
                      for x in args.config))
build_configs = set(cfg.build_config for cfg in run_configs)

make_targets = []
languages = set(_LANGUAGES[l] for l in args.language)
build_steps = [['make',
                '-j', '%d' % (multiprocessing.cpu_count() + 1),
                'CONFIG=%s' % cfg] + list(set(
                    itertools.chain.from_iterable(l.make_targets()
                                                  for l in languages)))
               for cfg in build_configs] + list(
                   itertools.chain.from_iterable(l.build_steps()
                                                 for l in languages))

runs_per_test = args.runs_per_test
forever = args.forever


class TestCache(object):
  """Cache for running tests."""

  def __init__(self):
    self._last_successful_run = {}

  def should_run(self, cmdline, bin_hash):
    cmdline = ' '.join(cmdline)
    if cmdline not in self._last_successful_run:
      return True
    if self._last_successful_run[cmdline] != bin_hash:
      return True
    return False

  def finished(self, cmdline, bin_hash):
    self._last_successful_run[' '.join(cmdline)] = bin_hash

  def dump(self):
    return [{'cmdline': k, 'hash': v}
            for k, v in self._last_successful_run.iteritems()]

  def parse(self, exdump):
    self._last_successful_run = dict((o['cmdline'], o['hash']) for o in exdump)

  def save(self):
    with open('.run_tests_cache', 'w') as f:
      f.write(json.dumps(self.dump()))

  def maybe_load(self):
    if os.path.exists('.run_tests_cache'):
      with open('.run_tests_cache') as f:
        self.parse(json.loads(f.read()))


def _build_and_run(check_cancelled, newline_on_success, cache):
  """Do one pass of building & running tests."""
  # build latest sequentially
  if not jobset.run(build_steps, maxjobs=1):
    return 1

  # run all the tests
  one_run = dict(
      (' '.join(config.run_command(x)), config.run_command(x))
      for config in run_configs
      for language in args.language
      for x in _LANGUAGES[language].test_binaries(config.build_config)
      ).values()
  all_runs = itertools.chain.from_iterable(
      itertools.repeat(one_run, runs_per_test))
  if not jobset.run(all_runs, check_cancelled,
                    newline_on_success=newline_on_success,
                    maxjobs=min(c.maxjobs for c in run_configs),
                    cache=cache):
    return 2

  return 0


test_cache = (None
              if not all(x.allow_hashing
                         for x in itertools.chain(languages, run_configs))
              else TestCache())
if test_cache:
  test_cache.maybe_load()

if forever:
  success = True
  while True:
    dw = watch_dirs.DirWatcher(['src', 'include', 'test'])
    initial_time = dw.most_recent_change()
    have_files_changed = lambda: dw.most_recent_change() != initial_time
    previous_success = success
    success = _build_and_run(check_cancelled=have_files_changed,
                             newline_on_success=False,
                             cache=test_cache) == 0
    if not previous_success and success:
      jobset.message('SUCCESS',
                     'All tests are now passing properly',
                     do_newline=True)
    jobset.message('IDLE', 'No change detected')
    if test_cache: test_cache.save()
    while not have_files_changed():
      time.sleep(1)
else:
  result = _build_and_run(check_cancelled=lambda: False,
                          newline_on_success=args.newline_on_success,
                          cache=test_cache)
  if result == 0:
    jobset.message('SUCCESS', 'All tests passed', do_newline=True)
  else:
    jobset.message('FAILED', 'Some tests failed', do_newline=True)
  if test_cache: test_cache.save()
  sys.exit(result)
