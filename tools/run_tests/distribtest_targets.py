#!/usr/bin/env python
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

  docker_args=[]
  for k,v in environ.iteritems():
    docker_args += ['-e', '%s=%s' % (k, v)]
  docker_env = {'DOCKERFILE_DIR': dockerfile_dir,
                'DOCKER_RUN_SCRIPT': 'tools/jenkins/docker_run.sh'}
  jobspec = jobset.JobSpec(
          cmdline=['tools/jenkins/build_and_run_docker.sh'] + docker_args,
          environ=docker_env,
          shortname='distribtest.%s' % (name),
          timeout_seconds=30*60,
          flake_retries=flake_retries,
          timeout_retries=timeout_retries)
  return jobspec


class CSharpDistribTest:
  """Tests C# NuGet package"""

  def __init__(self, platform, arch, docker_suffix):
    self.name = 'csharp_nuget_%s_%s_%s' % (platform, arch, docker_suffix)
    self.platform = platform
    self.arch = arch
    self.docker_suffix = docker_suffix
    self.labels = ['distribtest', 'csharp', platform, arch, docker_suffix]

  def pre_build_jobspecs(self):
    return []

  def build_jobspec(self):
    if not self.platform == 'linux':
      raise Exception("Not supported yet.")

    return create_docker_jobspec(self.name,
          'tools/dockerfile/distribtest/csharp_%s_%s' % (
              self.docker_suffix,
              self.arch),
          'test/distrib/csharp/run_distrib_test.sh')

  def __str__(self):
    return self.name


class PythonDistribTest:
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


def targets():
  """Gets list of supported targets"""
  return [CSharpDistribTest('linux', 'x64', 'wheezy'),
          CSharpDistribTest('linux', 'x64', 'jessie'),
          CSharpDistribTest('linux', 'x86', 'jessie'),
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
          ]

