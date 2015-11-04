#!/usr/bin/env python2.7
# Copyright 2015, Google Inc.
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

"""Run tests in parallel."""

import argparse
import glob
import hashlib
import itertools
import json
import multiprocessing
import os
import platform
import random
import re
import socket
import subprocess
import sys
import tempfile
import traceback
import time
import urllib2

import jobset
import report_utils
import watch_dirs

ROOT = os.path.abspath(os.path.join(os.path.dirname(sys.argv[0]), '../..'))
os.chdir(ROOT)


_FORCE_ENVIRON_FOR_WRAPPERS = {}


def platform_string():
  if platform.system() == 'Windows':
    return 'windows'
  elif platform.system() == 'Darwin':
    return 'mac'
  elif platform.system() == 'Linux':
    return 'linux'
  else:
    return 'posix'


# SimpleConfig: just compile with CONFIG=config, and run the binary to test
class SimpleConfig(object):

  def __init__(self, config, environ=None, timeout_multiplier=1):
    if environ is None:
      environ = {}
    self.build_config = config
    self.allow_hashing = (config != 'gcov')
    self.environ = environ
    self.environ['CONFIG'] = config
    self.timeout_multiplier = timeout_multiplier

  def job_spec(self, cmdline, hash_targets, timeout_seconds=5*60,
               shortname=None, environ={}):
    """Construct a jobset.JobSpec for a test under this config

       Args:
         cmdline:      a list of strings specifying the command line the test
                       would like to run
         hash_targets: either None (don't do caching of test results), or
                       a list of strings specifying files to include in a
                       binary hash to check if a test has changed
                       -- if used, all artifacts needed to run the test must
                          be listed
    """
    actual_environ = self.environ.copy()
    for k, v in environ.iteritems():
      actual_environ[k] = v
    return jobset.JobSpec(cmdline=cmdline,
                          shortname=shortname,
                          environ=actual_environ,
                          timeout_seconds=self.timeout_multiplier * timeout_seconds,
                          hash_targets=hash_targets
                              if self.allow_hashing else None,
                          flake_retries=5 if args.allow_flakes else 0,
                          timeout_retries=3 if args.allow_flakes else 0)


# ValgrindConfig: compile with some CONFIG=config, but use valgrind to run
class ValgrindConfig(object):

  def __init__(self, config, tool, args=None):
    if args is None:
      args = []
    self.build_config = config
    self.tool = tool
    self.args = args
    self.allow_hashing = False

  def job_spec(self, cmdline, hash_targets):
    return jobset.JobSpec(cmdline=['valgrind', '--tool=%s' % self.tool] +
                          self.args + cmdline,
                          shortname='valgrind %s' % cmdline[0],
                          hash_targets=None,
                          flake_retries=5 if args.allow_flakes else 0,
                          timeout_retries=3 if args.allow_flakes else 0)


def get_c_tests(travis, test_lang) :
  out = []
  platforms_str = 'ci_platforms' if travis else 'platforms'
  with open('tools/run_tests/tests.json') as f:
    js = json.load(f)
    return [tgt
            for tgt in js
            if tgt['language'] == test_lang and
                platform_string() in tgt[platforms_str] and
                not (travis and tgt['flaky'])]


class CLanguage(object):

  def __init__(self, make_target, test_lang):
    self.make_target = make_target
    self.platform = platform_string()
    self.test_lang = test_lang

  def test_specs(self, config, args):
    out = []
    binaries = get_c_tests(args.travis, self.test_lang)
    for target in binaries:
      if config.build_config in target['exclude_configs']:
        continue
      if self.platform == 'windows':
        binary = 'vsprojects/%s/%s.exe' % (
            _WINDOWS_CONFIG[config.build_config], target['name'])
      else:
        binary = 'bins/%s/%s' % (config.build_config, target['name'])
      if os.path.isfile(binary):
        out.append(config.job_spec([binary], [binary]))
      elif args.regex == '.*' or platform_string() == 'windows':
        print '\nWARNING: binary not found, skipping', binary
    return sorted(out)

  def make_targets(self, test_regex):
    if platform_string() != 'windows' and test_regex != '.*':
      # use the regex to minimize the number of things to build
      return [target['name']
              for target in get_c_tests(False, self.test_lang)
              if re.search(test_regex, target['name'])]
    if platform_string() == 'windows':
      # don't build tools on windows just yet
      return ['buildtests_%s' % self.make_target]
    return ['buildtests_%s' % self.make_target, 'tools_%s' % self.make_target]

  def pre_build_steps(self):
    if self.platform == 'windows':
      return [['tools\\run_tests\\pre_build_c.bat']]
    else:
      return []

  def build_steps(self):
    return []

  def post_tests_steps(self):
    if self.platform == 'windows':
      return []
    else:
      return [['tools/run_tests/post_tests_c.sh']]

  def makefile_name(self):
    return 'Makefile'

  def supports_multi_config(self):
    return True

  def __str__(self):
    return self.make_target

class NodeLanguage(object):

  def test_specs(self, config, args):
    return [config.job_spec(['tools/run_tests/run_node.sh'], None,
                            environ=_FORCE_ENVIRON_FOR_WRAPPERS)]

  def pre_build_steps(self):
    # Default to 1 week cache expiration
    return [['tools/run_tests/pre_build_node.sh']]

  def make_targets(self, test_regex):
    return []

  def build_steps(self):
    return [['tools/run_tests/build_node.sh']]

  def post_tests_steps(self):
    return []

  def makefile_name(self):
    return 'Makefile'

  def supports_multi_config(self):
    return False

  def __str__(self):
    return 'node'


class PhpLanguage(object):

  def test_specs(self, config, args):
    return [config.job_spec(['src/php/bin/run_tests.sh'], None,
                            environ=_FORCE_ENVIRON_FOR_WRAPPERS)]

  def pre_build_steps(self):
    return []

  def make_targets(self, test_regex):
    return ['static_c', 'shared_c']

  def build_steps(self):
    return [['tools/run_tests/build_php.sh']]

  def post_tests_steps(self):
    return []

  def makefile_name(self):
    return 'Makefile'

  def supports_multi_config(self):
    return False

  def __str__(self):
    return 'php'


class PythonLanguage(object):

  def __init__(self):
    self._build_python_versions = ['2.7']
    self._has_python_versions = []

  def test_specs(self, config, args):
    environment = dict(_FORCE_ENVIRON_FOR_WRAPPERS)
    environment['PYVER'] = '2.7'
    return [config.job_spec(
        ['tools/run_tests/run_python.sh'],
        None,
        environ=environment,
        shortname='py.test',
        timeout_seconds=15*60
    )]

  def pre_build_steps(self):
    return []

  def make_targets(self, test_regex):
    return ['static_c', 'grpc_python_plugin', 'shared_c']

  def build_steps(self):
    commands = []
    for python_version in self._build_python_versions:
      try:
        with open(os.devnull, 'w') as output:
          subprocess.check_call(['which', 'python' + python_version],
                                stdout=output, stderr=output)
        commands.append(['tools/run_tests/build_python.sh', python_version])
        self._has_python_versions.append(python_version)
      except:
        jobset.message('WARNING', 'Missing Python ' + python_version,
                       do_newline=True)
    return commands

  def post_tests_steps(self):
    return []

  def makefile_name(self):
    return 'Makefile'

  def supports_multi_config(self):
    return False

  def __str__(self):
    return 'python'


class RubyLanguage(object):

  def test_specs(self, config, args):
    return [config.job_spec(['tools/run_tests/run_ruby.sh'], None,
                            environ=_FORCE_ENVIRON_FOR_WRAPPERS)]

  def pre_build_steps(self):
    return [['tools/run_tests/pre_build_ruby.sh']]

  def make_targets(self, test_regex):
    return ['static_c']

  def build_steps(self):
    return [['tools/run_tests/build_ruby.sh']]

  def post_tests_steps(self):
    return []

  def makefile_name(self):
    return 'Makefile'

  def supports_multi_config(self):
    return False

  def __str__(self):
    return 'ruby'


class CSharpLanguage(object):
  def __init__(self):
    self.platform = platform_string()

  def test_specs(self, config, args):
    assemblies = ['Grpc.Core.Tests',
                  'Grpc.Examples.Tests',
                  'Grpc.HealthCheck.Tests',
                  'Grpc.IntegrationTesting']
    if self.platform == 'windows':
      cmd = 'tools\\run_tests\\run_csharp.bat'
    else:
      cmd = 'tools/run_tests/run_csharp.sh'

    if config.build_config == 'gcov':
      # On Windows, we only collect C# code coverage.
      # On Linux, we only collect coverage for native extension.
      # For code coverage all tests need to run as one suite.
      return [config.job_spec([cmd], None,
              environ=_FORCE_ENVIRON_FOR_WRAPPERS)]
    else:
      return [config.job_spec([cmd, assembly],
              None, shortname=assembly,
              environ=_FORCE_ENVIRON_FOR_WRAPPERS)
              for assembly in assemblies]

  def pre_build_steps(self):
    if self.platform == 'windows':
      return [['tools\\run_tests\\pre_build_csharp.bat']]
    else:
      return [['tools/run_tests/pre_build_csharp.sh']]

  def make_targets(self, test_regex):
    # For Windows, this target doesn't really build anything,
    # everything is build by buildall script later.
    if self.platform == 'windows':
      return []
    else:
      return ['grpc_csharp_ext']

  def build_steps(self):
    if self.platform == 'windows':
      return [['src\\csharp\\buildall.bat']]
    else:
      return [['tools/run_tests/build_csharp.sh']]

  def post_tests_steps(self):
    return []

  def makefile_name(self):
    return 'Makefile'

  def supports_multi_config(self):
    return False

  def __str__(self):
    return 'csharp'


class ObjCLanguage(object):

  def test_specs(self, config, args):
    return [config.job_spec(['src/objective-c/tests/run_tests.sh'], None,
                            environ=_FORCE_ENVIRON_FOR_WRAPPERS)]

  def pre_build_steps(self):
    return []

  def make_targets(self, test_regex):
    return ['grpc_objective_c_plugin', 'interop_server']

  def build_steps(self):
    return [['src/objective-c/tests/build_tests.sh']]

  def post_tests_steps(self):
    return []

  def makefile_name(self):
    return 'Makefile'

  def supports_multi_config(self):
    return False

  def __str__(self):
    return 'objc'


class Sanity(object):

  def test_specs(self, config, args):
    return [config.job_spec(['tools/run_tests/run_sanity.sh'], None),
            config.job_spec(['tools/run_tests/check_sources_and_headers.py'], None)]

  def pre_build_steps(self):
    return []

  def make_targets(self, test_regex):
    return ['run_dep_checks']

  def build_steps(self):
    return []

  def post_tests_steps(self):
    return []

  def makefile_name(self):
    return 'Makefile'

  def supports_multi_config(self):
    return False

  def __str__(self):
    return 'sanity'


class Build(object):

  def test_specs(self, config, args):
    return []

  def pre_build_steps(self):
    return []

  def make_targets(self, test_regex):
    return ['static']

  def build_steps(self):
    return []

  def post_tests_steps(self):
    return []

  def makefile_name(self):
    return 'Makefile'

  def supports_multi_config(self):
    return True

  def __str__(self):
    return self.make_target


# different configurations we can run under
_CONFIGS = {
    'dbg': SimpleConfig('dbg'),
    'opt': SimpleConfig('opt'),
    'tsan': SimpleConfig('tsan', timeout_multiplier=2, environ={
        'TSAN_OPTIONS': 'suppressions=tools/tsan_suppressions.txt:halt_on_error=1:second_deadlock_stack=1'}),
    'msan': SimpleConfig('msan', timeout_multiplier=1.5),
    'ubsan': SimpleConfig('ubsan'),
    'asan': SimpleConfig('asan', timeout_multiplier=1.5, environ={
        'ASAN_OPTIONS': 'detect_leaks=1:color=always:suppressions=tools/tsan_suppressions.txt',
        'LSAN_OPTIONS': 'report_objects=1'}),
    'asan-noleaks': SimpleConfig('asan', environ={
        'ASAN_OPTIONS': 'detect_leaks=0:color=always:suppressions=tools/tsan_suppressions.txt'}),
    'gcov': SimpleConfig('gcov'),
    'memcheck': ValgrindConfig('valgrind', 'memcheck', ['--leak-check=full']),
    'helgrind': ValgrindConfig('dbg', 'helgrind')
    }


_DEFAULT = ['opt']
_LANGUAGES = {
    'c++': CLanguage('cxx', 'c++'),
    'c': CLanguage('c', 'c'),
    'node': NodeLanguage(),
    'php': PhpLanguage(),
    'python': PythonLanguage(),
    'ruby': RubyLanguage(),
    'csharp': CSharpLanguage(),
    'objc' : ObjCLanguage(),
    'sanity': Sanity(),
    'build': Build(),
    }

_WINDOWS_CONFIG = {
    'dbg': 'Debug',
    'opt': 'Release',
    }


def runs_per_test_type(arg_str):
    """Auxilary function to parse the "runs_per_test" flag.

       Returns:
           A positive integer or 0, the latter indicating an infinite number of
           runs.

       Raises:
           argparse.ArgumentTypeError: Upon invalid input.
    """
    if arg_str == 'inf':
        return 0
    try:
        n = int(arg_str)
        if n <= 0: raise ValueError
        return n
    except:
        msg = '\'{}\' is not a positive integer or \'inf\''.format(arg_str)
        raise argparse.ArgumentTypeError(msg)

# parse command line
argp = argparse.ArgumentParser(description='Run grpc tests.')
argp.add_argument('-c', '--config',
                  choices=['all'] + sorted(_CONFIGS.keys()),
                  nargs='+',
                  default=_DEFAULT)
argp.add_argument('-n', '--runs_per_test', default=1, type=runs_per_test_type,
        help='A positive integer or "inf". If "inf", all tests will run in an '
             'infinite loop. Especially useful in combination with "-f"')
argp.add_argument('-r', '--regex', default='.*', type=str)
argp.add_argument('-j', '--jobs', default=2 * multiprocessing.cpu_count(), type=int)
argp.add_argument('-s', '--slowdown', default=1.0, type=float)
argp.add_argument('-f', '--forever',
                  default=False,
                  action='store_const',
                  const=True)
argp.add_argument('-t', '--travis',
                  default=False,
                  action='store_const',
                  const=True)
argp.add_argument('--newline_on_success',
                  default=False,
                  action='store_const',
                  const=True)
argp.add_argument('-l', '--language',
                  choices=['all'] + sorted(_LANGUAGES.keys()),
                  nargs='+',
                  default=['all'])
argp.add_argument('-S', '--stop_on_failure',
                  default=False,
                  action='store_const',
                  const=True)
argp.add_argument('--use_docker',
                  default=False,
                  action='store_const',
                  const=True,
                  help='Run all the tests under docker. That provides ' +
                  'additional isolation and prevents the need to install ' +
                  'language specific prerequisites. Only available on Linux.')
argp.add_argument('--allow_flakes',
                  default=False,
                  action='store_const',
                  const=True,
                  help='Allow flaky tests to show as passing (re-runs failed tests up to five times)')
argp.add_argument('-a', '--antagonists', default=0, type=int)
argp.add_argument('-x', '--xml_report', default=None, type=str,
        help='Generates a JUnit-compatible XML report')
args = argp.parse_args()

if args.use_docker:
  if not args.travis:
    print 'Seen --use_docker flag, will run tests under docker.'
    print
    print 'IMPORTANT: The changes you are testing need to be locally committed'
    print 'because only the committed changes in the current branch will be'
    print 'copied to the docker environment.'
    time.sleep(5)

  child_argv = [ arg for arg in sys.argv if not arg == '--use_docker' ]
  run_tests_cmd = 'tools/run_tests/run_tests.py %s' % ' '.join(child_argv[1:])

  # TODO(jtattermusch): revisit if we need special handling for arch here
  # set arch command prefix in case we are working with different arch.
  arch_env = os.getenv('arch')
  if arch_env:
    run_test_cmd = 'arch %s %s' % (arch_env, run_test_cmd)

  env = os.environ.copy()
  env['RUN_TESTS_COMMAND'] = run_tests_cmd
  if args.xml_report:
    env['XML_REPORT'] = args.xml_report
  if not args.travis:
    env['TTY_FLAG'] = '-t'  # enables Ctrl-C when not on Jenkins.

  subprocess.check_call(['tools/jenkins/build_docker_and_run_tests.sh'],
                        shell=True,
                        env=env)
  sys.exit(0)

# grab config
run_configs = set(_CONFIGS[cfg]
                  for cfg in itertools.chain.from_iterable(
                      _CONFIGS.iterkeys() if x == 'all' else [x]
                      for x in args.config))
build_configs = set(cfg.build_config for cfg in run_configs)

if args.travis:
  _FORCE_ENVIRON_FOR_WRAPPERS = {'GRPC_TRACE': 'api'}

languages = set(_LANGUAGES[l]
                for l in itertools.chain.from_iterable(
                      _LANGUAGES.iterkeys() if x == 'all' else [x]
                      for x in args.language))

if len(build_configs) > 1:
  for language in languages:
    if not language.supports_multi_config():
      print language, 'does not support multiple build configurations'
      sys.exit(1)

if platform.system() == 'Windows':
  def make_jobspec(cfg, targets, makefile='Makefile'):
    extra_args = []
    # better do parallel compilation
    # empirically /m:2 gives the best performance/price and should prevent
    # overloading the windows workers.
    extra_args.extend(['/m:2'])
    # disable PDB generation: it's broken, and we don't need it during CI
    extra_args.extend(['/p:Jenkins=true'])
    return [
      jobset.JobSpec(['vsprojects\\build.bat',
                      'vsprojects\\%s.sln' % target,
                      '/p:Configuration=%s' % _WINDOWS_CONFIG[cfg]] +
                      extra_args,
                      shell=True, timeout_seconds=90*60)
      for target in targets]
else:
  def make_jobspec(cfg, targets, makefile='Makefile'):
    if targets:
      return [jobset.JobSpec([os.getenv('MAKE', 'make'),
                              '-f', makefile,
                              '-j', '%d' % (multiprocessing.cpu_count() + 1),
                              'EXTRA_DEFINES=GRPC_TEST_SLOWDOWN_MACHINE_FACTOR=%f' %
                              args.slowdown,
                              'CONFIG=%s' % cfg] + targets,
                             timeout_seconds=30*60)]
    else:
      return []
make_targets = {}
for l in languages:
  makefile = l.makefile_name()
  make_targets[makefile] = make_targets.get(makefile, set()).union(
      set(l.make_targets(args.regex)))

build_steps = list(set(
                   jobset.JobSpec(cmdline, environ={'CONFIG': cfg}, flake_retries=5)
                   for cfg in build_configs
                   for l in languages
                   for cmdline in l.pre_build_steps()))
if make_targets:
  make_commands = itertools.chain.from_iterable(make_jobspec(cfg, list(targets), makefile) for cfg in build_configs for (makefile, targets) in make_targets.iteritems())
  build_steps.extend(set(make_commands))
build_steps.extend(set(
                   jobset.JobSpec(cmdline, environ={'CONFIG': cfg}, timeout_seconds=10*60)
                   for cfg in build_configs
                   for l in languages
                   for cmdline in l.build_steps()))

post_tests_steps = list(set(
                        jobset.JobSpec(cmdline, environ={'CONFIG': cfg})
                        for cfg in build_configs
                        for l in languages
                        for cmdline in l.post_tests_steps()))
runs_per_test = args.runs_per_test
forever = args.forever


class TestCache(object):
  """Cache for running tests."""

  def __init__(self, use_cache_results):
    self._last_successful_run = {}
    self._use_cache_results = use_cache_results
    self._last_save = time.time()

  def should_run(self, cmdline, bin_hash):
    if cmdline not in self._last_successful_run:
      return True
    if self._last_successful_run[cmdline] != bin_hash:
      return True
    if not self._use_cache_results:
      return True
    return False

  def finished(self, cmdline, bin_hash):
    self._last_successful_run[cmdline] = bin_hash
    if time.time() - self._last_save > 1:
      self.save()

  def dump(self):
    return [{'cmdline': k, 'hash': v}
            for k, v in self._last_successful_run.iteritems()]

  def parse(self, exdump):
    self._last_successful_run = dict((o['cmdline'], o['hash']) for o in exdump)

  def save(self):
    with open('.run_tests_cache', 'w') as f:
      f.write(json.dumps(self.dump()))
    self._last_save = time.time()

  def maybe_load(self):
    if os.path.exists('.run_tests_cache'):
      with open('.run_tests_cache') as f:
        self.parse(json.loads(f.read()))


def _start_port_server(port_server_port):
  # check if a compatible port server is running
  # if incompatible (version mismatch) ==> start a new one
  # if not running ==> start a new one
  # otherwise, leave it up
  try:
    version = int(urllib2.urlopen(
        'http://localhost:%d/version_number' % port_server_port,
        timeout=1).read())
    print 'detected port server running version %d' % version
    running = True
  except Exception as e:
    print 'failed to detect port server: %s' % sys.exc_info()[0]
    print e.strerror
    running = False
  if running:
    current_version = int(subprocess.check_output(
        [sys.executable, 'tools/run_tests/port_server.py', 'dump_version']))
    print 'my port server is version %d' % current_version
    running = (version >= current_version)
    if not running:
      print 'port_server version mismatch: killing the old one'
      urllib2.urlopen('http://localhost:%d/quitquitquit' % port_server_port).read()
      time.sleep(1)
  if not running:
    fd, logfile = tempfile.mkstemp()
    os.close(fd)
    print 'starting port_server, with log file %s' % logfile
    args = [sys.executable, 'tools/run_tests/port_server.py', '-p', '%d' % port_server_port, '-l', logfile]
    env = dict(os.environ)
    env['BUILD_ID'] = 'pleaseDontKillMeJenkins'
    if platform.system() == 'Windows':
      port_server = subprocess.Popen(
          args,
          env=env,
          creationflags = 0x00000008, # detached process
          close_fds=True)
    else:
      port_server = subprocess.Popen(
          args,
          env=env,
          preexec_fn=os.setsid,
          close_fds=True)
    time.sleep(1)
    # ensure port server is up
    waits = 0
    while True:
      if waits > 10:
        print 'killing port server due to excessive start up waits'
        port_server.kill()
      if port_server.poll() is not None:
        print 'port_server failed to start'
        # try one final time: maybe another build managed to start one
        time.sleep(1)
        try:
          urllib2.urlopen('http://localhost:%d/get' % port_server_port,
                          timeout=1).read()
          print 'last ditch attempt to contact port server succeeded'
          break
        except:
          traceback.print_exc();
          port_log = open(logfile, 'r').read()
          print port_log
          sys.exit(1)
      try:
        urllib2.urlopen('http://localhost:%d/get' % port_server_port,
                        timeout=1).read()
        print 'port server is up and ready'
        break
      except socket.timeout:
        print 'waiting for port_server: timeout'
        traceback.print_exc();
        time.sleep(1)
        waits += 1
      except urllib2.URLError:
        print 'waiting for port_server: urlerror'
        traceback.print_exc();
        time.sleep(1)
        waits += 1
      except:
        traceback.print_exc();
        port_server.kill()
        raise


def _calculate_num_runs_failures(list_of_results):
  """Caculate number of runs and failures for a particular test.

  Args:
    list_of_results: (List) of JobResult object.
  Returns:
    A tuple of total number of runs and failures.
  """
  num_runs = len(list_of_results)  # By default, there is 1 run per JobResult.
  num_failures = 0
  for jobresult in list_of_results:
    if jobresult.retries > 0:
      num_runs += jobresult.retries
    if jobresult.num_failures > 0:
      num_failures += jobresult.num_failures
  return num_runs, num_failures

def _build_and_run(
    check_cancelled, newline_on_success, cache, xml_report=None):
  """Do one pass of building & running tests."""
  # build latest sequentially
  num_failures, _ = jobset.run(
      build_steps, maxjobs=1, stop_on_failure=True,
      newline_on_success=newline_on_success, travis=args.travis)
  if num_failures:
    return 1

  # start antagonists
  antagonists = [subprocess.Popen(['tools/run_tests/antagonist.py'])
                 for _ in range(0, args.antagonists)]
  port_server_port = 32767
  _start_port_server(port_server_port)
  resultset = None
  try:
    infinite_runs = runs_per_test == 0
    one_run = set(
      spec
      for config in run_configs
      for language in languages
      for spec in language.test_specs(config, args)
      if re.search(args.regex, spec.shortname))
    # When running on travis, we want out test runs to be as similar as possible
    # for reproducibility purposes.
    if args.travis:
      massaged_one_run = sorted(one_run, key=lambda x: x.shortname)
    else:
      # whereas otherwise, we want to shuffle things up to give all tests a
      # chance to run.
      massaged_one_run = list(one_run)  # random.shuffle needs an indexable seq.
      random.shuffle(massaged_one_run)  # which it modifies in-place.
    if infinite_runs:
      assert len(massaged_one_run) > 0, 'Must have at least one test for a -n inf run'
    runs_sequence = (itertools.repeat(massaged_one_run) if infinite_runs
                     else itertools.repeat(massaged_one_run, runs_per_test))
    all_runs = itertools.chain.from_iterable(runs_sequence)

    number_failures, resultset = jobset.run(
        all_runs, check_cancelled, newline_on_success=newline_on_success,
        travis=args.travis, infinite_runs=infinite_runs, maxjobs=args.jobs,
        stop_on_failure=args.stop_on_failure,
        cache=cache if not xml_report else None,
        add_env={'GRPC_TEST_PORT_SERVER': 'localhost:%d' % port_server_port})
    if resultset:
      for k, v in resultset.iteritems():
        num_runs, num_failures = _calculate_num_runs_failures(v)
        if num_failures == num_runs:  # what about infinite_runs???
          jobset.message('FAILED', k, do_newline=True)
        elif num_failures > 0:
          jobset.message(
              'FLAKE', '%s [%d/%d runs flaked]' % (k, num_failures, num_runs),
              do_newline=True)
        else:
          jobset.message('PASSED', k, do_newline=True)
    if number_failures:
      return 2
  finally:
    for antagonist in antagonists:
      antagonist.kill()
    if xml_report and resultset:
      report_utils.render_xml_report(resultset, xml_report)

  number_failures, _ = jobset.run(
      post_tests_steps, maxjobs=1, stop_on_failure=True,
      newline_on_success=newline_on_success, travis=args.travis)
  if number_failures:
    return 3

  if cache: cache.save()

  return 0


test_cache = TestCache(runs_per_test == 1)
test_cache.maybe_load()

if forever:
  success = True
  while True:
    dw = watch_dirs.DirWatcher(['src', 'include', 'test', 'examples'])
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
    while not have_files_changed():
      time.sleep(1)
else:
  result = _build_and_run(check_cancelled=lambda: False,
                          newline_on_success=args.newline_on_success,
                          cache=test_cache,
                          xml_report=args.xml_report)
  if result == 0:
    jobset.message('SUCCESS', 'All tests passed', do_newline=True)
  else:
    jobset.message('FAILED', 'Some tests failed', do_newline=True)
  sys.exit(result)
