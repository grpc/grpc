#!/usr/bin/env python2.7
# Copyright 2016, Google Inc.
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

"""Definition of targets run distribution package tests."""

import jobset


def create_docker_jobspec(name, dockerfile_dir, shell_command, environ={},
                   flake_retries=0, timeout_retries=0):
  """Creates jobspec for a task running under docker."""
  environ = environ.copy()
  environ['RUN_COMMAND'] = shell_command
  environ['RELATIVE_COPY_PATH'] = 'test/distrib'

  docker_args=[]
  for k,v in environ.iteritems():
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
                   flake_retries=0, timeout_retries=0):
  """Creates jobspec."""
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

  def __init__(self, platform, arch, docker_suffix=None):
    self.name = 'csharp_nuget_%s_%s' % (platform, arch)
    self.platform = platform
    self.arch = arch
    self.docker_suffix = docker_suffix
    self.labels = ['distribtest', 'csharp', platform, arch]
    if docker_suffix:
      self.name += '_%s' % docker_suffix
      self.labels.append(docker_suffix)

  def pre_build_jobspecs(self):
    return []

  def build_jobspec(self):
    if self.platform == 'linux':
      return create_docker_jobspec(self.name,
          'tools/dockerfile/distribtest/csharp_%s_%s' % (
              self.docker_suffix,
              self.arch),
          'test/distrib/csharp/run_distrib_test.sh')
    elif self.platform == 'macos':
      return create_jobspec(self.name,
          ['test/distrib/csharp/run_distrib_test.sh'],
          environ={'EXTERNAL_GIT_ROOT': '../../..'})
    elif self.platform == 'windows':
      if self.arch == 'x64':
        environ={'MSBUILD_EXTRA_ARGS': '/p:Platform=x64',
                 'DISTRIBTEST_OUTPATH': 'DistribTest\\bin\\x64\\Debug'}
      else:
        environ={'DISTRIBTEST_OUTPATH': 'DistribTest\\bin\\\Debug'}
      return create_jobspec(self.name,
          ['test\\distrib\\csharp\\run_distrib_test.bat'],
          environ=environ)
    else:
      raise Exception("Not supported yet.")

  def __str__(self):
    return self.name

class NodeDistribTest(object):
  """Tests Node package"""

  def __init__(self, platform, arch, docker_suffix, node_version):
    self.name = 'node_npm_%s_%s_%s' % (platform, arch, node_version)
    self.platform = platform
    self.arch = arch
    self.node_version = node_version
    self.labels = ['distribtest', 'node', platform, arch,
                   'node-%s' % node_version]
    if docker_suffix is not None:
      self.name += '_%s' % docker_suffix
      self.docker_suffix = docker_suffix
      self.labels.append(docker_suffix)

  def pre_build_jobspecs(self):
    return []

  def build_jobspec(self):
    if self.platform == 'linux':
      linux32 = ''
      if self.arch == 'x86':
        linux32 = 'linux32'
      return create_docker_jobspec(self.name,
                                   'tools/dockerfile/distribtest/node_%s_%s' % (
                                       self.docker_suffix,
                                       self.arch),
                                   '%s test/distrib/node/run_distrib_test.sh %s' % (
                                       linux32,
                                       self.node_version))
    elif self.platform == 'macos':
      return create_jobspec(self.name,
                            ['test/distrib/node/run_distrib_test.sh',
                             str(self.node_version)],
                            environ={'EXTERNAL_GIT_ROOT': '../../..'})
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
          'test/distrib/python/run_distrib_test.sh')

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
          'test/distrib/ruby/run_distrib_test.sh')

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
                                   'test/distrib/php/run_distrib_test.sh')
    elif self.platform == 'macos':
      return create_jobspec(self.name,
          ['test/distrib/php/run_distrib_test.sh'],
          environ={'EXTERNAL_GIT_ROOT': '../../..'})
    else:
      raise Exception("Not supported yet.")

  def __str__(self):
    return self.name


def targets():
  """Gets list of supported targets"""
  return [CSharpDistribTest('linux', 'x64', 'wheezy'),
          CSharpDistribTest('linux', 'x64', 'jessie'),
          CSharpDistribTest('linux', 'x86', 'jessie'),
          CSharpDistribTest('linux', 'x64', 'centos7'),
          CSharpDistribTest('linux', 'x64', 'ubuntu1404'),
          CSharpDistribTest('linux', 'x64', 'ubuntu1504'),
          CSharpDistribTest('linux', 'x64', 'ubuntu1510'),
          CSharpDistribTest('linux', 'x64', 'ubuntu1604'),
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
          NodeDistribTest('macos', 'x64', None, '4'),
          NodeDistribTest('macos', 'x64', None, '5'),
          NodeDistribTest('linux', 'x86', 'jessie', '4'),
          PHPDistribTest('linux', 'x64', 'jessie'),
          PHPDistribTest('macos', 'x64'),
          ] + [
            NodeDistribTest('linux', 'x64', os, version)
            for os in ('wheezy', 'jessie', 'ubuntu1204', 'ubuntu1404',
                       'ubuntu1504', 'ubuntu1510', 'ubuntu1604')
            for version in ('0.12', '3', '4', '5')
          ]
