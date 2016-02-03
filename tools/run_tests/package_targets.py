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

"""Definition of targets to build distribution packages."""

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
                'DOCKER_RUN_SCRIPT': 'tools/jenkins/docker_run.sh',
                'OUTPUT_DIR': 'artifacts'}
  jobspec = jobset.JobSpec(
          cmdline=['tools/jenkins/build_and_run_docker.sh'] + docker_args,
          environ=docker_env,
          shortname='build_artifact.%s' % (name),
          timeout_seconds=30*60,
          flake_retries=flake_retries,
          timeout_retries=timeout_retries)
  return jobspec

def create_jobspec(name, cmdline, environ=None, cwd=None, shell=False,
                   flake_retries=0, timeout_retries=0):
  """Creates jobspec."""
  jobspec = jobset.JobSpec(
          cmdline=cmdline,
          environ=environ,
          cwd=cwd,
          shortname='build_package.%s' % (name),
          timeout_seconds=10*60,
          flake_retries=flake_retries,
          timeout_retries=timeout_retries,
          shell=shell)
  return jobspec


class CSharpNugetTarget:
  """Builds C# nuget packages."""

  def __init__(self):
    self.name = 'csharp_nuget'
    self.labels = ['package', 'csharp', 'windows']

  def pre_build_jobspecs(self):
    return []

  def build_jobspec(self):
    return create_jobspec(self.name,
                          ['build_packages.bat'],
                          cwd='src\\csharp',
                          shell=True)

  def __str__(self):
    return self.name

class NodeNpmBinaryTarget:
  """Builds Node NPM package and collects binaries"""

  def __init__(self):
    self.name = 'node_npm_binary'
    self.labels = ['package', 'csharp', 'windows']

  def pre_build_jobspecs(self):
    return []

  def build_jobspec(self):
    return create_docker_jobspec(
        self.name,
        'tools/dockerfile/grpc_artifact_linux_x64',
        'tools/run_tests/build_package_node.sh')

def targets():
  """Gets list of supported targets"""
  return [CSharpNugetTarget(), NodeNpmBinaryTarget()]
