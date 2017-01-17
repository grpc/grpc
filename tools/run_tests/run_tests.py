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

from __future__ import print_function

import argparse
import ast
import collections
import glob
import itertools
import json
import multiprocessing
import os
import os.path
import pipes
import platform
import random
import re
import socket
import subprocess
import sys
import tempfile
import traceback
import time
from six.moves import urllib
import uuid

import python_utils.jobset as jobset
import python_utils.report_utils as report_utils
import python_utils.watch_dirs as watch_dirs


_ROOT = os.path.abspath(os.path.join(os.path.dirname(sys.argv[0]), '../..'))
os.chdir(_ROOT)


_FORCE_ENVIRON_FOR_WRAPPERS = {
  'GRPC_VERBOSITY': 'DEBUG',
}


_POLLING_STRATEGIES = {
  'linux': ['epoll', 'poll', 'poll-cv']
}


def platform_string():
  return jobset.platform_string()


_DEFAULT_TIMEOUT_SECONDS = 5 * 60


# SimpleConfig: just compile with CONFIG=config, and run the binary to test
class Config(object):

  def __init__(self, config, environ=None, timeout_multiplier=1, tool_prefix=[], iomgr_platform='native'):
    if environ is None:
      environ = {}
    self.build_config = config
    self.environ = environ
    self.environ['CONFIG'] = config
    self.tool_prefix = tool_prefix
    self.timeout_multiplier = timeout_multiplier
    self.iomgr_platform = iomgr_platform

  def job_spec(self, cmdline, timeout_seconds=_DEFAULT_TIMEOUT_SECONDS,
               shortname=None, environ={}, cpu_cost=1.0, flaky=False):
    """Construct a jobset.JobSpec for a test under this config

       Args:
         cmdline:      a list of strings specifying the command line the test
                       would like to run
    """
    actual_environ = self.environ.copy()
    for k, v in environ.items():
      actual_environ[k] = v
    return jobset.JobSpec(cmdline=self.tool_prefix + cmdline,
                          shortname=shortname,
                          environ=actual_environ,
                          cpu_cost=cpu_cost,
                          timeout_seconds=(self.timeout_multiplier * timeout_seconds if timeout_seconds else None),
                          flake_retries=5 if flaky or args.allow_flakes else 0,
                          timeout_retries=3 if args.allow_flakes else 0)


def get_c_tests(travis, test_lang) :
  out = []
  platforms_str = 'ci_platforms' if travis else 'platforms'
  with open('tools/run_tests/generated/tests.json') as f:
    js = json.load(f)
    return [tgt
            for tgt in js
            if tgt['language'] == test_lang and
                platform_string() in tgt[platforms_str] and
                not (travis and tgt['flaky'])]


def _check_compiler(compiler, supported_compilers):
  if compiler not in supported_compilers:
    raise Exception('Compiler %s not supported (on this platform).' % compiler)


def _check_arch(arch, supported_archs):
  if arch not in supported_archs:
    raise Exception('Architecture %s not supported.' % arch)


def _is_use_docker_child():
  """Returns True if running running as a --use_docker child."""
  return True if os.getenv('RUN_TESTS_COMMAND') else False


_PythonConfigVars = collections.namedtuple(
  '_ConfigVars', ['shell', 'builder', 'builder_prefix_arguments',
                  'venv_relative_python', 'toolchain', 'runner'])


def _python_config_generator(name, major, minor, bits, config_vars):
  return PythonConfig(
    name,
    config_vars.shell + config_vars.builder + config_vars.builder_prefix_arguments + [
      _python_pattern_function(major=major, minor=minor, bits=bits)] + [
      name] + config_vars.venv_relative_python + config_vars.toolchain,
    config_vars.shell + config_vars.runner + [
      os.path.join(name, config_vars.venv_relative_python[0])])


def _pypy_config_generator(name, major, config_vars):
  return PythonConfig(
    name,
    config_vars.shell + config_vars.builder + config_vars.builder_prefix_arguments + [
      _pypy_pattern_function(major=major)] + [
      name] + config_vars.venv_relative_python + config_vars.toolchain,
    config_vars.shell + config_vars.runner + [
      os.path.join(name, config_vars.venv_relative_python[0])])


def _python_pattern_function(major, minor, bits):
  # Bit-ness is handled by the test machine's environment
  if os.name == "nt":
    if bits == "64":
      return '/c/Python{major}{minor}/python.exe'.format(
        major=major, minor=minor, bits=bits)
    else:
      return '/c/Python{major}{minor}_{bits}bits/python.exe'.format(
        major=major, minor=minor, bits=bits)
  else:
    return 'python{major}.{minor}'.format(major=major, minor=minor)


def _pypy_pattern_function(major):
  if major == '2':
    return 'pypy'
  elif major == '3':
    return 'pypy3'
  else:
    raise ValueError("Unknown PyPy major version")


class CLanguage(object):

  def __init__(self, make_target, test_lang):
    self.make_target = make_target
    self.platform = platform_string()
    self.test_lang = test_lang

  def configure(self, config, args):
    self.config = config
    self.args = args
    if self.platform == 'windows':
      self._make_options = [_windows_toolset_option(self.args.compiler),
                            _windows_arch_option(self.args.arch)]
    else:
      self._docker_distro, self._make_options = self._compiler_options(self.args.use_docker,
                                                                       self.args.compiler)
    if args.iomgr_platform == "uv":
      cflags = '-DGRPC_UV '
      try:
        cflags += subprocess.check_output(['pkg-config', '--cflags', 'libuv']).strip() + ' '
      except (subprocess.CalledProcessError, OSError):
        pass
      try:
        ldflags = subprocess.check_output(['pkg-config', '--libs', 'libuv']).strip() + ' '
      except (subprocess.CalledProcessError, OSError):
        ldflags = '-luv '
      self._make_options += ['EXTRA_CPPFLAGS={}'.format(cflags),
                             'EXTRA_LDLIBS={}'.format(ldflags)]

  def test_specs(self):
    out = []
    binaries = get_c_tests(self.args.travis, self.test_lang)
    for target in binaries:
      polling_strategies = (_POLLING_STRATEGIES.get(self.platform, ['all'])
                            if target.get('uses_polling', True)
                            else ['all'])
      if self.args.iomgr_platform == 'uv':
        polling_strategies = ['all']
      for polling_strategy in polling_strategies:
        env={'GRPC_DEFAULT_SSL_ROOTS_FILE_PATH':
                 _ROOT + '/src/core/lib/tsi/test_creds/ca.pem',
             'GRPC_POLL_STRATEGY': polling_strategy,
             'GRPC_VERBOSITY': 'DEBUG'}
        shortname_ext = '' if polling_strategy=='all' else ' GRPC_POLL_STRATEGY=%s' % polling_strategy
        timeout_scaling = 1
        if polling_strategy == 'poll-cv':
          timeout_scaling *= 5
        if self.config.build_config in target['exclude_configs']:
          continue
        if self.args.iomgr_platform in target.get('exclude_iomgrs', []):
          continue
        if self.platform == 'windows':
          binary = 'vsprojects/%s%s/%s.exe' % (
              'x64/' if self.args.arch == 'x64' else '',
              _MSBUILD_CONFIG[self.config.build_config],
              target['name'])
        else:
          binary = 'bins/%s/%s' % (self.config.build_config, target['name'])
        cpu_cost = target['cpu_cost']
        if cpu_cost == 'capacity':
          cpu_cost = multiprocessing.cpu_count()
        if os.path.isfile(binary):
          if 'gtest' in target and target['gtest']:
            # here we parse the output of --gtest_list_tests to build up a
            # complete list of the tests contained in a binary
            # for each test, we then add a job to run, filtering for just that
            # test
            with open(os.devnull, 'w') as fnull:
              tests = subprocess.check_output([binary, '--gtest_list_tests'],
                                              stderr=fnull)
            base = None
            for line in tests.split('\n'):
              i = line.find('#')
              if i >= 0: line = line[:i]
              if not line: continue
              if line[0] != ' ':
                base = line.strip()
              else:
                assert base is not None
                assert line[1] == ' '
                test = base + line.strip()
                cmdline = [binary] + ['--gtest_filter=%s' % test]
                out.append(self.config.job_spec(cmdline,
                                                shortname='%s --gtest_filter=%s %s' % (binary, test, shortname_ext),
                                                cpu_cost=cpu_cost,
                                                timeout_seconds=_DEFAULT_TIMEOUT_SECONDS * timeout_scaling,
                                                environ=env))
          else:
            cmdline = [binary] + target['args']
            out.append(self.config.job_spec(cmdline,
                                            shortname=' '.join(
                                                          pipes.quote(arg)
                                                          for arg in cmdline) +
                                                      shortname_ext,
                                            cpu_cost=cpu_cost,
                                            flaky=target.get('flaky', False),
                                            timeout_seconds=target.get('timeout_seconds', _DEFAULT_TIMEOUT_SECONDS) * timeout_scaling,
                                            environ=env))
        elif self.args.regex == '.*' or self.platform == 'windows':
          print('\nWARNING: binary not found, skipping', binary)
    return sorted(out)

  def make_targets(self):
    if self.platform == 'windows':
      # don't build tools on windows just yet
      return ['buildtests_%s' % self.make_target]
    return ['buildtests_%s' % self.make_target, 'tools_%s' % self.make_target]

  def make_options(self):
    return self._make_options;

  def pre_build_steps(self):
    if self.platform == 'windows':
      return [['tools\\run_tests\\helper_scripts\\pre_build_c.bat']]
    else:
      return []

  def build_steps(self):
    return []

  def post_tests_steps(self):
    if self.platform == 'windows':
      return []
    else:
      return [['tools/run_tests/helper_scripts/post_tests_c.sh']]

  def makefile_name(self):
    return 'Makefile'

  def _clang_make_options(self, version_suffix=''):
    return ['CC=clang%s' % version_suffix,
            'CXX=clang++%s' % version_suffix,
            'LD=clang%s' % version_suffix,
            'LDXX=clang++%s' % version_suffix]

  def _gcc_make_options(self, version_suffix):
    return ['CC=gcc%s' % version_suffix,
            'CXX=g++%s' % version_suffix,
            'LD=gcc%s' % version_suffix,
            'LDXX=g++%s' % version_suffix]

  def _compiler_options(self, use_docker, compiler):
    """Returns docker distro and make options to use for given compiler."""
    if not use_docker and not _is_use_docker_child():
      _check_compiler(compiler, ['default'])

    if compiler == 'gcc4.9' or compiler == 'default':
      return ('jessie', [])
    elif compiler == 'gcc4.4':
      return ('wheezy', self._gcc_make_options(version_suffix='-4.4'))
    elif compiler == 'gcc4.6':
      return ('wheezy', self._gcc_make_options(version_suffix='-4.6'))
    elif compiler == 'gcc4.8':
      return ('jessie', self._gcc_make_options(version_suffix='-4.8'))
    elif compiler == 'gcc5.3':
      return ('ubuntu1604', [])
    elif compiler == 'clang3.4':
      # on ubuntu1404, clang-3.4 alias doesn't exist, just use 'clang'
      return ('ubuntu1404', self._clang_make_options())
    elif compiler == 'clang3.5':
      return ('jessie', self._clang_make_options(version_suffix='-3.5'))
    elif compiler == 'clang3.6':
      return ('ubuntu1604', self._clang_make_options(version_suffix='-3.6'))
    elif compiler == 'clang3.7':
      return ('ubuntu1604', self._clang_make_options(version_suffix='-3.7'))
    else:
      raise Exception('Compiler %s not supported.' % compiler)

  def dockerfile_dir(self):
    return 'tools/dockerfile/test/cxx_%s_%s' % (self._docker_distro,
                                                _docker_arch_suffix(self.args.arch))

  def __str__(self):
    return self.make_target


class NodeLanguage(object):

  def __init__(self):
    self.platform = platform_string()

  def configure(self, config, args):
    self.config = config
    self.args = args
    # Note: electron ABI only depends on major and minor version, so that's all
    # we should specify in the compiler argument
    _check_compiler(self.args.compiler, ['default', 'node0.12',
                                         'node4', 'node5', 'node6',
                                         'node7', 'electron1.3'])
    if self.args.compiler == 'default':
      self.runtime = 'node'
      self.node_version = '4'
    else:
      if self.args.compiler.startswith('electron'):
        self.runtime = 'electron'
        self.node_version = self.args.compiler[8:]
      else:
        self.runtime = 'node'
        # Take off the word "node"
        self.node_version = self.args.compiler[4:]

  def test_specs(self):
    if self.platform == 'windows':
      return [self.config.job_spec(['tools\\run_tests\\helper_scripts\\run_node.bat'])]
    else:
      run_script = 'run_node'
      if self.runtime == 'electron':
        run_script += '_electron'
      return [self.config.job_spec(['tools/run_tests/helper_scripts/{}.sh'.format(run_script),
                                    self.node_version],
                                   None,
                                   environ=_FORCE_ENVIRON_FOR_WRAPPERS)]

  def pre_build_steps(self):
    if self.platform == 'windows':
      return [['tools\\run_tests\\helper_scripts\\pre_build_node.bat']]
    else:
      build_script = 'pre_build_node'
      if self.runtime == 'electron':
        build_script += '_electron'
      return [['tools/run_tests/helper_scripts/{}.sh'.format(build_script), self.node_version]]

  def make_targets(self):
    return []

  def make_options(self):
    return []

  def build_steps(self):
    if self.platform == 'windows':
      return [['tools\\run_tests\\helper_scripts\\build_node.bat']]
    else:
      build_script = 'build_node'
      if self.runtime == 'electron':
        build_script += '_electron'
        # building for electron requires a patch version
        self.node_version += '.0'
      return [['tools/run_tests/helper_scripts/{}.sh'.format(build_script), self.node_version]]

  def post_tests_steps(self):
    return []

  def makefile_name(self):
    return 'Makefile'

  def dockerfile_dir(self):
    return 'tools/dockerfile/test/node_jessie_%s' % _docker_arch_suffix(self.args.arch)

  def __str__(self):
    return 'node'


class PhpLanguage(object):

  def configure(self, config, args):
    self.config = config
    self.args = args
    _check_compiler(self.args.compiler, ['default'])

  def test_specs(self):
    return [self.config.job_spec(['src/php/bin/run_tests.sh'],
                                  environ=_FORCE_ENVIRON_FOR_WRAPPERS)]

  def pre_build_steps(self):
    return []

  def make_targets(self):
    return ['static_c', 'shared_c']

  def make_options(self):
    return []

  def build_steps(self):
    return [['tools/run_tests/helper_scripts/build_php.sh']]

  def post_tests_steps(self):
    return [['tools/run_tests/helper_scripts/post_tests_php.sh']]

  def makefile_name(self):
    return 'Makefile'

  def dockerfile_dir(self):
    return 'tools/dockerfile/test/php_jessie_%s' % _docker_arch_suffix(self.args.arch)

  def __str__(self):
    return 'php'


class Php7Language(object):

  def configure(self, config, args):
    self.config = config
    self.args = args
    _check_compiler(self.args.compiler, ['default'])

  def test_specs(self):
    return [self.config.job_spec(['src/php/bin/run_tests.sh'],
                                  environ=_FORCE_ENVIRON_FOR_WRAPPERS)]

  def pre_build_steps(self):
    return []

  def make_targets(self):
    return ['static_c', 'shared_c']

  def make_options(self):
    return []

  def build_steps(self):
    return [['tools/run_tests/helper_scripts/build_php.sh']]

  def post_tests_steps(self):
    return [['tools/run_tests/helper_scripts/post_tests_php.sh']]

  def makefile_name(self):
    return 'Makefile'

  def dockerfile_dir(self):
    return 'tools/dockerfile/test/php7_jessie_%s' % _docker_arch_suffix(self.args.arch)

  def __str__(self):
    return 'php7'


class PythonConfig(collections.namedtuple('PythonConfig', [
    'name', 'build', 'run'])):
  """Tuple of commands (named s.t. 'what it says on the tin' applies)"""

class PythonLanguage(object):

  def configure(self, config, args):
    self.config = config
    self.args = args
    self.pythons = self._get_pythons(self.args)

  def test_specs(self):
    # load list of known test suites
    with open('src/python/grpcio_tests/tests/tests.json') as tests_json_file:
      tests_json = json.load(tests_json_file)
    environment = dict(_FORCE_ENVIRON_FOR_WRAPPERS)
    return [self.config.job_spec(
        config.run,
        timeout_seconds=5*60,
        environ=dict(list(environment.items()) +
                     [('GRPC_PYTHON_TESTRUNNER_FILTER', str(suite_name))]),
        shortname='%s.test.%s' % (config.name, suite_name),)
        for suite_name in tests_json
        for config in self.pythons]

  def pre_build_steps(self):
    return []

  def make_targets(self):
    return []

  def make_options(self):
    return []

  def build_steps(self):
    return [config.build for config in self.pythons]

  def post_tests_steps(self):
    return []

  def makefile_name(self):
    return 'Makefile'

  def dockerfile_dir(self):
    return 'tools/dockerfile/test/python_%s_%s' % (self.python_manager_name(), _docker_arch_suffix(self.args.arch))

  def python_manager_name(self):
    return 'pyenv' if self.args.compiler in ['python3.5', 'python3.6'] else 'jessie'

  def _get_pythons(self, args):
    if args.arch == 'x86':
      bits = '32'
    else:
      bits = '64'

    if os.name == 'nt':
      shell = ['bash']
      builder = [os.path.abspath('tools/run_tests/helper_scripts/build_python_msys2.sh')]
      builder_prefix_arguments = ['MINGW{}'.format(bits)]
      venv_relative_python = ['Scripts/python.exe']
      toolchain = ['mingw32']
    else:
      shell = []
      builder = [os.path.abspath('tools/run_tests/helper_scripts/build_python.sh')]
      builder_prefix_arguments = []
      venv_relative_python = ['bin/python']
      toolchain = ['unix']

    runner = [os.path.abspath('tools/run_tests/helper_scripts/run_python.sh')]
    config_vars = _PythonConfigVars(shell, builder, builder_prefix_arguments,
                              venv_relative_python, toolchain, runner)
    python27_config = _python_config_generator(name='py27', major='2',
                                               minor='7', bits=bits,
                                               config_vars=config_vars)
    python34_config = _python_config_generator(name='py34', major='3',
                                               minor='4', bits=bits,
                                               config_vars=config_vars)
    python35_config = _python_config_generator(name='py35', major='3',
                                               minor='5', bits=bits,
                                               config_vars=config_vars)
    python36_config = _python_config_generator(name='py36', major='3',
                                               minor='6', bits=bits,
                                               config_vars=config_vars)
    pypy27_config = _pypy_config_generator(name='pypy', major='2',
                                           config_vars=config_vars)
    pypy32_config = _pypy_config_generator(name='pypy3', major='3',
                                           config_vars=config_vars)

    if args.compiler == 'default':
      if os.name == 'nt':
        return (python27_config,)
      else:
        return (python27_config, python34_config,)
    elif args.compiler == 'python2.7':
      return (python27_config,)
    elif args.compiler == 'python3.4':
      return (python34_config,)
    elif args.compiler == 'python3.5':
      return (python35_config,)
    elif args.compiler == 'python3.6':
      return (python36_config,)
    elif args.compiler == 'pypy':
      return (pypy27_config,)
    elif args.compiler == 'pypy3':
      return (pypy32_config,)
    else:
      raise Exception('Compiler %s not supported.' % args.compiler)

  def __str__(self):
    return 'python'


class RubyLanguage(object):

  def configure(self, config, args):
    self.config = config
    self.args = args
    _check_compiler(self.args.compiler, ['default'])

  def test_specs(self):
    return [self.config.job_spec(['tools/run_tests/helper_scripts/run_ruby.sh'],
                                 timeout_seconds=10*60,
                                 environ=_FORCE_ENVIRON_FOR_WRAPPERS)]

  def pre_build_steps(self):
    return [['tools/run_tests/helper_scripts/pre_build_ruby.sh']]

  def make_targets(self):
    return []

  def make_options(self):
    return []

  def build_steps(self):
    return [['tools/run_tests/helper_scripts/build_ruby.sh']]

  def post_tests_steps(self):
    return [['tools/run_tests/helper_scripts/post_tests_ruby.sh']]

  def makefile_name(self):
    return 'Makefile'

  def dockerfile_dir(self):
    return 'tools/dockerfile/test/ruby_jessie_%s' % _docker_arch_suffix(self.args.arch)

  def __str__(self):
    return 'ruby'


class CSharpLanguage(object):

  def __init__(self):
    self.platform = platform_string()

  def configure(self, config, args):
    self.config = config
    self.args = args
    if self.platform == 'windows':
      # Explicitly choosing between x86 and x64 arch doesn't work yet
      _check_arch(self.args.arch, ['default'])
      # CoreCLR use 64bit runtime by default.
      arch_option = 'x64' if self.args.compiler == 'coreclr' else self.args.arch
      self._make_options = [_windows_toolset_option(self.args.compiler),
                            _windows_arch_option(arch_option)]
    else:
      _check_compiler(self.args.compiler, ['default', 'coreclr'])
      if self.platform == 'linux' and self.args.compiler == 'coreclr':
        self._docker_distro = 'coreclr'
      else:
        self._docker_distro = 'jessie'

      if self.platform == 'mac':
        # TODO(jtattermusch): EMBED_ZLIB=true currently breaks the mac build
        self._make_options = ['EMBED_OPENSSL=true']
        if self.args.compiler != 'coreclr':
          # On Mac, official distribution of mono is 32bit.
          self._make_options += ['CFLAGS=-m32', 'LDFLAGS=-m32']
      else:
        self._make_options = ['EMBED_OPENSSL=true', 'EMBED_ZLIB=true']

  def test_specs(self):
    with open('src/csharp/tests.json') as f:
      tests_by_assembly = json.load(f)

    msbuild_config = _MSBUILD_CONFIG[self.config.build_config]
    nunit_args = ['--labels=All']
    assembly_subdir = 'bin/%s' % msbuild_config
    assembly_extension = '.exe'

    if self.args.compiler == 'coreclr':
      assembly_subdir += '/netcoreapp1.0'
      runtime_cmd = ['dotnet', 'exec']
      assembly_extension = '.dll'
    else:
      nunit_args += ['--noresult', '--workers=1']
      if self.platform == 'windows':
        runtime_cmd = []
      else:
        runtime_cmd = ['mono']

    specs = []
    for assembly in tests_by_assembly.iterkeys():
      assembly_file = 'src/csharp/%s/%s/%s%s' % (assembly,
                                                 assembly_subdir,
                                                 assembly,
                                                 assembly_extension)
      if self.config.build_config != 'gcov' or self.platform != 'windows':
        # normally, run each test as a separate process
        for test in tests_by_assembly[assembly]:
          cmdline = runtime_cmd + [assembly_file, '--test=%s' % test] + nunit_args
          specs.append(self.config.job_spec(cmdline,
                                            shortname='csharp.%s' % test,
                                            environ=_FORCE_ENVIRON_FOR_WRAPPERS))
      else:
        # For C# test coverage, run all tests from the same assembly at once
        # using OpenCover.Console (only works on Windows).
        cmdline = ['src\\csharp\\packages\\OpenCover.4.6.519\\tools\\OpenCover.Console.exe',
                   '-target:%s' % assembly_file,
                   '-targetdir:src\\csharp',
                   '-targetargs:%s' % ' '.join(nunit_args),
                   '-filter:+[Grpc.Core]*',
                   '-register:user',
                   '-output:src\\csharp\\coverage_csharp_%s.xml' % assembly]

        # set really high cpu_cost to make sure instances of OpenCover.Console run exclusively
        # to prevent problems with registering the profiler.
        run_exclusive = 1000000
        specs.append(self.config.job_spec(cmdline,
                                          shortname='csharp.coverage.%s' % assembly,
                                          cpu_cost=run_exclusive,
                                          environ=_FORCE_ENVIRON_FOR_WRAPPERS))
    return specs

  def pre_build_steps(self):
    if self.platform == 'windows':
      return [['tools\\run_tests\\helper_scripts\\pre_build_csharp.bat']]
    else:
      return [['tools/run_tests/helper_scripts/pre_build_csharp.sh']]

  def make_targets(self):
    return ['grpc_csharp_ext']

  def make_options(self):
    return self._make_options;

  def build_steps(self):
    if self.args.compiler == 'coreclr':
      if self.platform == 'windows':
        return [['tools\\run_tests\\helper_scripts\\build_csharp_coreclr.bat']]
      else:
        return [['tools/run_tests/helper_scripts/build_csharp_coreclr.sh']]
    else:
      if self.platform == 'windows':
        return [[_windows_build_bat(self.args.compiler),
                 'src/csharp/Grpc.sln',
                 '/p:Configuration=%s' % _MSBUILD_CONFIG[self.config.build_config]]]
      else:
        return [['tools/run_tests/helper_scripts/build_csharp.sh']]

  def post_tests_steps(self):
    if self.platform == 'windows':
      return [['tools\\run_tests\\helper_scripts\\post_tests_csharp.bat']]
    else:
      return [['tools/run_tests/helper_scripts/post_tests_csharp.sh']]

  def makefile_name(self):
    return 'Makefile'

  def dockerfile_dir(self):
    return 'tools/dockerfile/test/csharp_%s_%s' % (self._docker_distro,
                                                   _docker_arch_suffix(self.args.arch))

  def __str__(self):
    return 'csharp'


class ObjCLanguage(object):

  def configure(self, config, args):
    self.config = config
    self.args = args
    _check_compiler(self.args.compiler, ['default'])

  def test_specs(self):
    return [
        self.config.job_spec(['src/objective-c/tests/run_tests.sh'],
                              timeout_seconds=60*60,
                              shortname='objc-tests',
                              environ=_FORCE_ENVIRON_FOR_WRAPPERS),
        self.config.job_spec(['src/objective-c/tests/build_example_test.sh'],
                              timeout_seconds=30*60,
                              shortname='objc-examples-build',
                              environ=_FORCE_ENVIRON_FOR_WRAPPERS),
    ]

  def pre_build_steps(self):
    return []

  def make_targets(self):
    return ['interop_server']

  def make_options(self):
    return []

  def build_steps(self):
    return [['src/objective-c/tests/build_tests.sh']]

  def post_tests_steps(self):
    return []

  def makefile_name(self):
    return 'Makefile'

  def dockerfile_dir(self):
    return None

  def __str__(self):
    return 'objc'


class Sanity(object):

  def configure(self, config, args):
    self.config = config
    self.args = args
    _check_compiler(self.args.compiler, ['default'])

  def test_specs(self):
    import yaml
    with open('tools/run_tests/sanity/sanity_tests.yaml', 'r') as f:
      environ={'TEST': 'true'}
      if _is_use_docker_child():
        environ['CLANG_FORMAT_SKIP_DOCKER'] = 'true'
      return [self.config.job_spec(cmd['script'].split(),
                                   timeout_seconds=30*60,
                                   environ=environ,
                                   cpu_cost=cmd.get('cpu_cost', 1))
              for cmd in yaml.load(f)]

  def pre_build_steps(self):
    return []

  def make_targets(self):
    return ['run_dep_checks']

  def make_options(self):
    return []

  def build_steps(self):
    return []

  def post_tests_steps(self):
    return []

  def makefile_name(self):
    return 'Makefile'

  def dockerfile_dir(self):
    return 'tools/dockerfile/test/sanity'

  def __str__(self):
    return 'sanity'

class NodeExpressLanguage(object):
  """Dummy Node express test target to enable running express performance
  benchmarks"""

  def __init__(self):
    self.platform = platform_string()

  def configure(self, config, args):
    self.config = config
    self.args = args
    _check_compiler(self.args.compiler, ['default', 'node0.12',
                                         'node4', 'node5', 'node6'])
    if self.args.compiler == 'default':
      self.node_version = '4'
    else:
      # Take off the word "node"
      self.node_version = self.args.compiler[4:]

  def test_specs(self):
    return []

  def pre_build_steps(self):
    if self.platform == 'windows':
      return [['tools\\run_tests\\helper_scripts\\pre_build_node.bat']]
    else:
      return [['tools/run_tests/helper_scripts/pre_build_node.sh', self.node_version]]

  def make_targets(self):
    return []

  def make_options(self):
    return []

  def build_steps(self):
    return []

  def post_tests_steps(self):
    return []

  def makefile_name(self):
    return 'Makefile'

  def dockerfile_dir(self):
    return 'tools/dockerfile/test/node_jessie_%s' % _docker_arch_suffix(self.args.arch)

  def __str__(self):
    return 'node_express'

# different configurations we can run under
with open('tools/run_tests/generated/configs.json') as f:
  _CONFIGS = dict((cfg['config'], Config(**cfg)) for cfg in ast.literal_eval(f.read()))


_LANGUAGES = {
    'c++': CLanguage('cxx', 'c++'),
    'c': CLanguage('c', 'c'),
    'node': NodeLanguage(),
    'node_express': NodeExpressLanguage(),
    'php': PhpLanguage(),
    'php7': Php7Language(),
    'python': PythonLanguage(),
    'ruby': RubyLanguage(),
    'csharp': CSharpLanguage(),
    'objc' : ObjCLanguage(),
    'sanity': Sanity()
    }


_MSBUILD_CONFIG = {
    'dbg': 'Debug',
    'opt': 'Release',
    'gcov': 'Debug',
    }


def _windows_arch_option(arch):
  """Returns msbuild cmdline option for selected architecture."""
  if arch == 'default' or arch == 'x86':
    return '/p:Platform=Win32'
  elif arch == 'x64':
    return '/p:Platform=x64'
  else:
    print('Architecture %s not supported.' % arch)
    sys.exit(1)


def _check_arch_option(arch):
  """Checks that architecture option is valid."""
  if platform_string() == 'windows':
    _windows_arch_option(arch)
  elif platform_string() == 'linux':
    # On linux, we need to be running under docker with the right architecture.
    runtime_arch = platform.architecture()[0]
    if arch == 'default':
      return
    elif runtime_arch == '64bit' and arch == 'x64':
      return
    elif runtime_arch == '32bit' and arch == 'x86':
      return
    else:
      print('Architecture %s does not match current runtime architecture.' % arch)
      sys.exit(1)
  else:
    if args.arch != 'default':
      print('Architecture %s not supported on current platform.' % args.arch)
      sys.exit(1)


def _windows_build_bat(compiler):
  """Returns name of build.bat for selected compiler."""
  # For CoreCLR, fall back to the default compiler for C core
  if compiler == 'default' or compiler == 'vs2013' or compiler == 'coreclr':
    return 'vsprojects\\build_vs2013.bat'
  elif compiler == 'vs2015':
    return 'vsprojects\\build_vs2015.bat'
  elif compiler == 'vs2010':
    return 'vsprojects\\build_vs2010.bat'
  else:
    print('Compiler %s not supported.' % compiler)
    sys.exit(1)


def _windows_toolset_option(compiler):
  """Returns msbuild PlatformToolset for selected compiler."""
  # For CoreCLR, fall back to the default compiler for C core
  if compiler == 'default' or compiler == 'vs2013' or compiler == 'coreclr':
    return '/p:PlatformToolset=v120'
  elif compiler == 'vs2015':
    return '/p:PlatformToolset=v140'
  elif compiler == 'vs2010':
    return '/p:PlatformToolset=v100'
  else:
    print('Compiler %s not supported.' % compiler)
    sys.exit(1)


def _docker_arch_suffix(arch):
  """Returns suffix to dockerfile dir to use."""
  if arch == 'default' or arch == 'x64':
    return 'x64'
  elif arch == 'x86':
    return 'x86'
  else:
    print('Architecture %s not supported with current settings.' % arch)
    sys.exit(1)


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
                  choices=sorted(_CONFIGS.keys()),
                  default='opt')
argp.add_argument('-n', '--runs_per_test', default=1, type=runs_per_test_type,
        help='A positive integer or "inf". If "inf", all tests will run in an '
             'infinite loop. Especially useful in combination with "-f"')
argp.add_argument('-r', '--regex', default='.*', type=str)
argp.add_argument('--regex_exclude', default='', type=str)
argp.add_argument('-j', '--jobs', default=multiprocessing.cpu_count(), type=int)
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
argp.add_argument('--arch',
                  choices=['default', 'x86', 'x64'],
                  default='default',
                  help='Selects architecture to target. For some platforms "default" is the only supported choice.')
argp.add_argument('--compiler',
                  choices=['default',
                           'gcc4.4', 'gcc4.6', 'gcc4.8', 'gcc4.9', 'gcc5.3',
                           'clang3.4', 'clang3.5', 'clang3.6', 'clang3.7',
                           'vs2010', 'vs2013', 'vs2015',
                           'python2.7', 'python3.4', 'python3.5', 'python3.6', 'pypy', 'pypy3',
                           'node0.12', 'node4', 'node5', 'node6', 'node7',
                           'electron1.3',
                           'coreclr'],
                  default='default',
                  help='Selects compiler to use. Allowed values depend on the platform and language.')
argp.add_argument('--iomgr_platform',
                  choices=['native', 'uv'],
                  default='native',
                  help='Selects iomgr platform to build on')
argp.add_argument('--build_only',
                  default=False,
                  action='store_const',
                  const=True,
                  help='Perform all the build steps but dont run any tests.')
argp.add_argument('--measure_cpu_costs', default=False, action='store_const', const=True,
                  help='Measure the cpu costs of tests')
argp.add_argument('--update_submodules', default=[], nargs='*',
                  help='Update some submodules before building. If any are updated, also run generate_projects. ' +
                       'Submodules are specified as SUBMODULE_NAME:BRANCH; if BRANCH is omitted, master is assumed.')
argp.add_argument('-a', '--antagonists', default=0, type=int)
argp.add_argument('-x', '--xml_report', default=None, type=str,
        help='Generates a JUnit-compatible XML report')
argp.add_argument('--report_suite_name', default='tests', type=str,
        help='Test suite name to use in generated JUnit XML report')
argp.add_argument('--quiet_success',
                  default=False,
                  action='store_const',
                  const=True,
                  help='Dont print anything when a test passes. Passing tests also will not be reported in XML report. ' +
                       'Useful when running many iterations of each test (argument -n).')
argp.add_argument('--force_default_poller', default=False, action='store_const', const=True,
                  help='Dont try to iterate over many polling strategies when they exist')
args = argp.parse_args()

if args.force_default_poller:
  _POLLING_STRATEGIES = {}

jobset.measure_cpu_costs = args.measure_cpu_costs

# update submodules if necessary
need_to_regenerate_projects = False
for spec in args.update_submodules:
  spec = spec.split(':', 1)
  if len(spec) == 1:
    submodule = spec[0]
    branch = 'master'
  elif len(spec) == 2:
    submodule = spec[0]
    branch = spec[1]
  cwd = 'third_party/%s' % submodule
  def git(cmd, cwd=cwd):
    print('in %s: git %s' % (cwd, cmd))
    subprocess.check_call('git %s' % cmd, cwd=cwd, shell=True)
  git('fetch')
  git('checkout %s' % branch)
  git('pull origin %s' % branch)
  if os.path.exists('src/%s/gen_build_yaml.py' % submodule):
    need_to_regenerate_projects = True
if need_to_regenerate_projects:
  if jobset.platform_string() == 'linux':
    subprocess.check_call('tools/buildgen/generate_projects.sh', shell=True)
  else:
    print('WARNING: may need to regenerate projects, but since we are not on')
    print('         Linux this step is being skipped. Compilation MAY fail.')


# grab config
run_config = _CONFIGS[args.config]
build_config = run_config.build_config

if args.travis:
  _FORCE_ENVIRON_FOR_WRAPPERS = {'GRPC_TRACE': 'api'}

if 'all' in args.language:
  lang_list = _LANGUAGES.keys()
else:
  lang_list = args.language
# We don't support code coverage on some languages
if 'gcov' in args.config:
  for bad in ['objc', 'sanity']:
    if bad in lang_list:
      lang_list.remove(bad)

languages = set(_LANGUAGES[l] for l in lang_list)
for l in languages:
  l.configure(run_config, args)

language_make_options=[]
if any(language.make_options() for language in languages):
  if not 'gcov' in args.config and len(languages) != 1:
    print('languages with custom make options cannot be built simultaneously with other languages')
    sys.exit(1)
  else:
    language_make_options = next(iter(languages)).make_options()

if args.use_docker:
  if not args.travis:
    print('Seen --use_docker flag, will run tests under docker.')
    print('')
    print('IMPORTANT: The changes you are testing need to be locally committed')
    print('because only the committed changes in the current branch will be')
    print('copied to the docker environment.')
    time.sleep(5)

  dockerfile_dirs = set([l.dockerfile_dir() for l in languages])
  if len(dockerfile_dirs) > 1:
    if 'gcov' in args.config:
      dockerfile_dir = 'tools/dockerfile/test/multilang_jessie_x64'
      print ('Using multilang_jessie_x64 docker image for code coverage for '
             'all languages.')
    else:
      print ('Languages to be tested require running under different docker '
             'images.')
      sys.exit(1)
  else:
    dockerfile_dir = next(iter(dockerfile_dirs))

  child_argv = [ arg for arg in sys.argv if not arg == '--use_docker' ]
  run_tests_cmd = 'python tools/run_tests/run_tests.py %s' % ' '.join(child_argv[1:])

  env = os.environ.copy()
  env['RUN_TESTS_COMMAND'] = run_tests_cmd
  env['DOCKERFILE_DIR'] = dockerfile_dir
  env['DOCKER_RUN_SCRIPT'] = 'tools/run_tests/dockerize/docker_run_tests.sh'
  if args.xml_report:
    env['XML_REPORT'] = args.xml_report
  if not args.travis:
    env['TTY_FLAG'] = '-t'  # enables Ctrl-C when not on Jenkins.

  subprocess.check_call(['tools/run_tests/dockerize/build_docker_and_run_tests.sh'],
                        shell=True,
                        env=env)
  sys.exit(0)

_check_arch_option(args.arch)

def make_jobspec(cfg, targets, makefile='Makefile'):
  if platform_string() == 'windows':
    extra_args = []
    # better do parallel compilation
    # empirically /m:2 gives the best performance/price and should prevent
    # overloading the windows workers.
    extra_args.extend(['/m:2'])
    # disable PDB generation: it's broken, and we don't need it during CI
    extra_args.extend(['/p:Jenkins=true'])
    return [
      jobset.JobSpec([_windows_build_bat(args.compiler),
                      'vsprojects\\%s.sln' % target,
                      '/p:Configuration=%s' % _MSBUILD_CONFIG[cfg]] +
                      extra_args +
                      language_make_options,
                      shell=True, timeout_seconds=None)
      for target in targets]
  else:
    if targets:
      return [jobset.JobSpec([os.getenv('MAKE', 'make'),
                              '-f', makefile,
                              '-j', '%d' % args.jobs,
                              'EXTRA_DEFINES=GRPC_TEST_SLOWDOWN_MACHINE_FACTOR=%f' % args.slowdown,
                              'CONFIG=%s' % cfg] +
                              language_make_options +
                             ([] if not args.travis else ['JENKINS_BUILD=1']) +
                             targets,
                             timeout_seconds=None)]
    else:
      return []

make_targets = {}
for l in languages:
  makefile = l.makefile_name()
  make_targets[makefile] = make_targets.get(makefile, set()).union(
      set(l.make_targets()))

def build_step_environ(cfg):
  environ = {'CONFIG': cfg}
  msbuild_cfg = _MSBUILD_CONFIG.get(cfg)
  if msbuild_cfg:
    environ['MSBUILD_CONFIG'] = msbuild_cfg
  return environ

build_steps = list(set(
                   jobset.JobSpec(cmdline, environ=build_step_environ(build_config), flake_retries=5)
                   for l in languages
                   for cmdline in l.pre_build_steps()))
if make_targets:
  make_commands = itertools.chain.from_iterable(make_jobspec(build_config, list(targets), makefile) for (makefile, targets) in make_targets.items())
  build_steps.extend(set(make_commands))
build_steps.extend(set(
                   jobset.JobSpec(cmdline, environ=build_step_environ(build_config), timeout_seconds=None)
                   for l in languages
                   for cmdline in l.build_steps()))

post_tests_steps = list(set(
                        jobset.JobSpec(cmdline, environ=build_step_environ(build_config))
                        for l in languages
                        for cmdline in l.post_tests_steps()))
runs_per_test = args.runs_per_test
forever = args.forever


def _shut_down_legacy_server(legacy_server_port):
  try:
    version = int(urllib.request.urlopen(
        'http://localhost:%d/version_number' % legacy_server_port,
        timeout=10).read())
  except:
    pass
  else:
    urllib.request.urlopen(
        'http://localhost:%d/quitquitquit' % legacy_server_port).read()


def _start_port_server(port_server_port):
  # check if a compatible port server is running
  # if incompatible (version mismatch) ==> start a new one
  # if not running ==> start a new one
  # otherwise, leave it up
  try:
    version = int(urllib.request.urlopen(
        'http://localhost:%d/version_number' % port_server_port,
        timeout=10).read())
    print('detected port server running version %d' % version)
    running = True
  except Exception as e:
    print('failed to detect port server: %s' % sys.exc_info()[0])
    print(e.strerror)
    running = False
  if running:
    current_version = int(subprocess.check_output(
        [sys.executable, os.path.abspath('tools/run_tests/python_utils/port_server.py'),
         'dump_version']))
    print('my port server is version %d' % current_version)
    running = (version >= current_version)
    if not running:
      print('port_server version mismatch: killing the old one')
      urllib.request.urlopen('http://localhost:%d/quitquitquit' % port_server_port).read()
      time.sleep(1)
  if not running:
    fd, logfile = tempfile.mkstemp()
    os.close(fd)
    print('starting port_server, with log file %s' % logfile)
    args = [sys.executable, os.path.abspath('tools/run_tests/python_utils/port_server.py'),
            '-p', '%d' % port_server_port, '-l', logfile]
    env = dict(os.environ)
    env['BUILD_ID'] = 'pleaseDontKillMeJenkins'
    if platform_string() == 'windows':
      # Working directory of port server needs to be outside of Jenkins
      # workspace to prevent file lock issues.
      tempdir = tempfile.mkdtemp()
      port_server = subprocess.Popen(
          args,
          env=env,
          cwd=tempdir,
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
        print('killing port server due to excessive start up waits')
        port_server.kill()
      if port_server.poll() is not None:
        print('port_server failed to start')
        # try one final time: maybe another build managed to start one
        time.sleep(1)
        try:
          urllib.request.urlopen('http://localhost:%d/get' % port_server_port,
                          timeout=1).read()
          print('last ditch attempt to contact port server succeeded')
          break
        except:
          traceback.print_exc()
          port_log = open(logfile, 'r').read()
          print(port_log)
          sys.exit(1)
      try:
        urllib.request.urlopen('http://localhost:%d/get' % port_server_port,
                        timeout=1).read()
        print('port server is up and ready')
        break
      except socket.timeout:
        print('waiting for port_server: timeout')
        traceback.print_exc();
        time.sleep(1)
        waits += 1
      except urllib.error.URLError:
        print('waiting for port_server: urlerror')
        traceback.print_exc();
        time.sleep(1)
        waits += 1
      except:
        traceback.print_exc()
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


# _build_and_run results
class BuildAndRunError(object):

  BUILD = object()
  TEST = object()
  POST_TEST = object()


# returns a list of things that failed (or an empty list on success)
def _build_and_run(
    check_cancelled, newline_on_success, xml_report=None, build_only=False):
  """Do one pass of building & running tests."""
  # build latest sequentially
  num_failures, resultset = jobset.run(
      build_steps, maxjobs=1, stop_on_failure=True,
      newline_on_success=newline_on_success, travis=args.travis)
  if num_failures:
    return [BuildAndRunError.BUILD]

  if build_only:
    if xml_report:
      report_utils.render_junit_xml_report(resultset, xml_report,
                                           suite_name=args.report_suite_name)
    return []

  # start antagonists
  antagonists = [subprocess.Popen(['tools/run_tests/python_utils/antagonist.py'])
                 for _ in range(0, args.antagonists)]
  port_server_port = 32766
  _start_port_server(port_server_port)
  resultset = None
  num_test_failures = 0
  try:
    infinite_runs = runs_per_test == 0
    one_run = set(
      spec
      for language in languages
      for spec in language.test_specs()
      if (re.search(args.regex, spec.shortname) and
          (args.regex_exclude == '' or
           not re.search(args.regex_exclude, spec.shortname))))
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

    if args.quiet_success:
      jobset.message('START', 'Running tests quietly, only failing tests will be reported', do_newline=True)
    num_test_failures, resultset = jobset.run(
        all_runs, check_cancelled, newline_on_success=newline_on_success,
        travis=args.travis, infinite_runs=infinite_runs, maxjobs=args.jobs,
        stop_on_failure=args.stop_on_failure,
        add_env={'GRPC_TEST_PORT_SERVER': 'localhost:%d' % port_server_port},
        quiet_success=args.quiet_success)
    if resultset:
      for k, v in sorted(resultset.items()):
        num_runs, num_failures = _calculate_num_runs_failures(v)
        if num_failures > 0:
          if num_failures == num_runs:  # what about infinite_runs???
            jobset.message('FAILED', k, do_newline=True)
          else:
            jobset.message(
                'FLAKE', '%s [%d/%d runs flaked]' % (k, num_failures, num_runs),
                do_newline=True)
  finally:
    for antagonist in antagonists:
      antagonist.kill()
    if xml_report and resultset:
      report_utils.render_junit_xml_report(resultset, xml_report,
                                           suite_name=args.report_suite_name)

  number_failures, _ = jobset.run(
      post_tests_steps, maxjobs=1, stop_on_failure=True,
      newline_on_success=newline_on_success, travis=args.travis)

  out = []
  if number_failures:
    out.append(BuildAndRunError.POST_TEST)
  if num_test_failures:
    out.append(BuildAndRunError.TEST)

  return out


if forever:
  success = True
  while True:
    dw = watch_dirs.DirWatcher(['src', 'include', 'test', 'examples'])
    initial_time = dw.most_recent_change()
    have_files_changed = lambda: dw.most_recent_change() != initial_time
    previous_success = success
    errors = _build_and_run(check_cancelled=have_files_changed,
                            newline_on_success=False,
                            build_only=args.build_only) == 0
    if not previous_success and not errors:
      jobset.message('SUCCESS',
                     'All tests are now passing properly',
                     do_newline=True)
    jobset.message('IDLE', 'No change detected')
    while not have_files_changed():
      time.sleep(1)
else:
  errors = _build_and_run(check_cancelled=lambda: False,
                          newline_on_success=args.newline_on_success,
                          xml_report=args.xml_report,
                          build_only=args.build_only)
  if not errors:
    jobset.message('SUCCESS', 'All tests passed', do_newline=True)
  else:
    jobset.message('FAILED', 'Some tests failed', do_newline=True)
  exit_code = 0
  if BuildAndRunError.BUILD in errors:
    exit_code |= 1
  if BuildAndRunError.TEST in errors:
    exit_code |= 2
  if BuildAndRunError.POST_TEST in errors:
    exit_code |= 4
  sys.exit(exit_code)
