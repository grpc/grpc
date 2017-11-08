#!/usr/bin/env python
# Copyright 2016 gRPC authors.
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

"""Definition of targets run distribution package tests."""

import os.path
import sys

sys.path.insert(0, os.path.abspath('..'))
import python_utils.jobset as jobset


def create_docker_jobspec(name, dockerfile_dir, shell_command, environ={},
                   flake_retries=0, timeout_retries=0,
                   copy_rel_path=None):
  """Creates jobspec for a task running under docker."""
  environ = environ.copy()
  environ['RUN_COMMAND'] = shell_command
  # the entire repo will be cloned if copy_rel_path is not set.
  if copy_rel_path:
    environ['RELATIVE_COPY_PATH'] = copy_rel_path

  docker_args=[]
  for k,v in environ.items():
    docker_args += ['-e', '%s=%s' % (k, v)]
  docker_env = {'DOCKERFILE_DIR': dockerfile_dir,
                'DOCKER_RUN_SCRIPT': 'tools/run_tests/dockerize/docker_run.sh'}
  jobspec = jobset.JobSpec(
          cmdline=['tools/run_tests/dockerize/build_and_run_docker.sh'] + docker_args,
          environ=docker_env,
          shortname='distribtest.%s' % (name),
          timeout_seconds=30*60,
          flake_retries=flake_retries,
          timeout_retries=timeout_retries)
  return jobspec


def create_jobspec(name, cmdline, environ=None, shell=False,
                   flake_retries=0, timeout_retries=0,
                   use_workspace=False):
  """Creates jobspec."""
  environ = environ.copy()
  if use_workspace:
    environ['WORKSPACE_NAME'] = 'workspace_%s' % name
    cmdline = ['bash',
               'tools/run_tests/artifacts/run_in_workspace.sh'] + cmdline
  jobspec = jobset.JobSpec(
          cmdline=cmdline,
          environ=environ,
          shortname='distribtest.%s' % (name),
          timeout_seconds=10*60,
          flake_retries=flake_retries,
          timeout_retries=timeout_retries,
          shell=shell)
  return jobspec


class CSharpDistribTest(object):
  """Tests C# NuGet package"""

  def __init__(self, platform, arch, docker_suffix=None, use_dotnet_cli=False):
    self.name = 'csharp_%s_%s' % (platform, arch)
    self.platform = platform
    self.arch = arch
    self.docker_suffix = docker_suffix
    self.labels = ['distribtest', 'csharp', platform, arch]
    self.script_suffix = ''
    if docker_suffix:
      self.name += '_%s' % docker_suffix
      self.labels.append(docker_suffix)
    if use_dotnet_cli:
      self.name += '_dotnetcli'
      self.script_suffix = '_dotnetcli'
      self.labels.append('dotnetcli')
    else:
      self.labels.append('olddotnet')

  def pre_build_jobspecs(self):
    return []

  def build_jobspec(self):
    if self.platform == 'linux':
      return create_docker_jobspec(self.name,
          'tools/dockerfile/distribtest/csharp_%s_%s' % (
              self.docker_suffix,
              self.arch),
          'test/distrib/csharp/run_distrib_test%s.sh' % self.script_suffix,
          copy_rel_path='test/distrib')
    elif self.platform == 'macos':
      return create_jobspec(self.name,
          ['test/distrib/csharp/run_distrib_test%s.sh' % self.script_suffix],
          environ={'EXTERNAL_GIT_ROOT': '../../../..'},
          use_workspace=True)
    elif self.platform == 'windows':
      if self.arch == 'x64':
        # Use double leading / as the first occurence gets removed by msys bash
        # when invoking the .bat file (side-effect of posix path conversion)
        environ={'MSBUILD_EXTRA_ARGS': '//p:Platform=x64',
                 'DISTRIBTEST_OUTPATH': 'DistribTest\\bin\\x64\\Debug'}
      else:
        environ={'DISTRIBTEST_OUTPATH': 'DistribTest\\bin\\Debug'}
      return create_jobspec(self.name,
          ['test\\distrib\\csharp\\run_distrib_test%s.bat' % self.script_suffix],
          environ=environ,
          use_workspace=True)
    else:
      raise Exception("Not supported yet.")

  def __str__(self):
    return self.name

class PythonDistribTest(object):
  """Tests Python package"""

  def __init__(self, platform, arch, docker_suffix):
    self.name = 'python_%s_%s_%s' % (platform, arch, docker_suffix)
    self.platform = platform
    self.arch = arch
    self.docker_suffix = docker_suffix
    self.labels = ['distribtest', 'python', platform, arch, docker_suffix]

  def pre_build_jobspecs(self):
    return []

  def build_jobspec(self):
    if not self.platform == 'linux':
      raise Exception("Not supported yet.")

    return create_docker_jobspec(self.name,
          'tools/dockerfile/distribtest/python_%s_%s' % (
              self.docker_suffix,
              self.arch),
          'test/distrib/python/run_distrib_test.sh',
          copy_rel_path='test/distrib')

  def __str__(self):
    return self.name


class RubyDistribTest(object):
  """Tests Ruby package"""

  def __init__(self, platform, arch, docker_suffix):
    self.name = 'ruby_%s_%s_%s' % (platform, arch, docker_suffix)
    self.platform = platform
    self.arch = arch
    self.docker_suffix = docker_suffix
    self.labels = ['distribtest', 'ruby', platform, arch, docker_suffix]

  def pre_build_jobspecs(self):
    return []

  def build_jobspec(self):
    if not self.platform == 'linux':
      raise Exception("Not supported yet.")

    return create_docker_jobspec(self.name,
          'tools/dockerfile/distribtest/ruby_%s_%s' % (
              self.docker_suffix,
              self.arch),
          'test/distrib/ruby/run_distrib_test.sh',
          copy_rel_path='test/distrib')

  def __str__(self):
    return self.name


class PHPDistribTest(object):
  """Tests PHP package"""

  def __init__(self, platform, arch, docker_suffix=None):
    self.name = 'php_%s_%s_%s' % (platform, arch, docker_suffix)
    self.platform = platform
    self.arch = arch
    self.docker_suffix = docker_suffix
    self.labels = ['distribtest', 'php', platform, arch, docker_suffix]

  def pre_build_jobspecs(self):
    return []

  def build_jobspec(self):
    if self.platform == 'linux':
      return create_docker_jobspec(self.name,
                                   'tools/dockerfile/distribtest/php_%s_%s' % (
                                       self.docker_suffix,
                                       self.arch),
                                   'test/distrib/php/run_distrib_test.sh',
                                   copy_rel_path='test/distrib')
    elif self.platform == 'macos':
      return create_jobspec(self.name,
          ['test/distrib/php/run_distrib_test.sh'],
          environ={'EXTERNAL_GIT_ROOT': '../../../..'},
          use_workspace=True)
    else:
      raise Exception("Not supported yet.")

  def __str__(self):
    return self.name


class CppDistribTest(object):
  """Tests Cpp make intall by building examples."""

  def __init__(self, platform, arch, docker_suffix=None, testcase=None):
    self.name = 'cpp_%s_%s_%s_%s' % (platform, arch, docker_suffix, testcase)
    self.platform = platform
    self.arch = arch
    self.docker_suffix = docker_suffix
    self.testcase = testcase
    self.labels = ['distribtest', 'cpp', platform, arch, docker_suffix, testcase]

  def pre_build_jobspecs(self):
    return []

  def build_jobspec(self):
    if self.platform == 'linux':
      return create_docker_jobspec(self.name,
                                   'tools/dockerfile/distribtest/cpp_%s_%s' % (
                                       self.docker_suffix,
                                       self.arch),
                                   'test/distrib/cpp/run_distrib_test_%s.sh' % self.testcase)
    else:
      raise Exception("Not supported yet.")

  def __str__(self):
    return self.name


def targets():
  """Gets list of supported targets"""
  return [CppDistribTest('linux', 'x64', 'jessie', 'routeguide'),
          CppDistribTest('linux', 'x64', 'jessie', 'cmake'),
          CSharpDistribTest('linux', 'x64', 'wheezy'),
          CSharpDistribTest('linux', 'x64', 'jessie'),
          CSharpDistribTest('linux', 'x86', 'jessie'),
          CSharpDistribTest('linux', 'x64', 'centos7'),
          CSharpDistribTest('linux', 'x64', 'ubuntu1404'),
          CSharpDistribTest('linux', 'x64', 'ubuntu1504'),
          CSharpDistribTest('linux', 'x64', 'ubuntu1510'),
          CSharpDistribTest('linux', 'x64', 'ubuntu1604'),
          CSharpDistribTest('linux', 'x64', 'ubuntu1404', use_dotnet_cli=True),
          CSharpDistribTest('macos', 'x86'),
          CSharpDistribTest('windows', 'x86'),
          CSharpDistribTest('windows', 'x64'),
          PythonDistribTest('linux', 'x64', 'wheezy'),
          PythonDistribTest('linux', 'x64', 'jessie'),
          PythonDistribTest('linux', 'x86', 'jessie'),
          PythonDistribTest('linux', 'x64', 'centos6'),
          PythonDistribTest('linux', 'x64', 'centos7'),
          PythonDistribTest('linux', 'x64', 'fedora20'),
          PythonDistribTest('linux', 'x64', 'fedora21'),
          PythonDistribTest('linux', 'x64', 'fedora22'),
          PythonDistribTest('linux', 'x64', 'fedora23'),
          PythonDistribTest('linux', 'x64', 'opensuse'),
          PythonDistribTest('linux', 'x64', 'arch'),
          PythonDistribTest('linux', 'x64', 'ubuntu1204'),
          PythonDistribTest('linux', 'x64', 'ubuntu1404'),
          PythonDistribTest('linux', 'x64', 'ubuntu1504'),
          PythonDistribTest('linux', 'x64', 'ubuntu1510'),
          PythonDistribTest('linux', 'x64', 'ubuntu1604'),
          RubyDistribTest('linux', 'x64', 'wheezy'),
          RubyDistribTest('linux', 'x64', 'jessie'),
          RubyDistribTest('linux', 'x86', 'jessie'),
          RubyDistribTest('linux', 'x64', 'centos6'),
          RubyDistribTest('linux', 'x64', 'centos7'),
          RubyDistribTest('linux', 'x64', 'fedora20'),
          RubyDistribTest('linux', 'x64', 'fedora21'),
          RubyDistribTest('linux', 'x64', 'fedora22'),
          RubyDistribTest('linux', 'x64', 'fedora23'),
          RubyDistribTest('linux', 'x64', 'opensuse'),
          RubyDistribTest('linux', 'x64', 'ubuntu1204'),
          RubyDistribTest('linux', 'x64', 'ubuntu1404'),
          RubyDistribTest('linux', 'x64', 'ubuntu1504'),
          RubyDistribTest('linux', 'x64', 'ubuntu1510'),
          RubyDistribTest('linux', 'x64', 'ubuntu1604'),
          PHPDistribTest('linux', 'x64', 'jessie'),
          PHPDistribTest('macos', 'x64'),
          ]
