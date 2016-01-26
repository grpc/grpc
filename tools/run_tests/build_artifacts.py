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

"""Builds gRPC distribution artifacts."""

import argparse
import atexit
import dockerjob
import itertools
import jobset
import json
import multiprocessing
import os
import re
import subprocess
import sys
import time
import uuid

# Docker doesn't clean up after itself, so we do it on exit.
if jobset.platform_string() == 'linux':
  atexit.register(lambda: subprocess.call(['stty', 'echo']))

ROOT = os.path.abspath(os.path.join(os.path.dirname(sys.argv[0]), '../..'))
os.chdir(ROOT)


def create_docker_jobspec(name, dockerfile_dir, shell_command, environ={},
                   flake_retries=0, timeout_retries=0):
  """Creates jobspec for a task running under docker."""
  environ = environ.copy()
  environ['RUN_COMMAND'] = shell_command

  #docker_args = ['-v', '%s/artifacts:/var/local/jenkins/grpc/artifacts' % ROOT]
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


def create_jobspec(name, cmdline, environ=None, shell=False,
                   flake_retries=0, timeout_retries=0):
  """Creates jobspec."""
  jobspec = jobset.JobSpec(
          cmdline=cmdline,
          environ=environ,
          shortname='build_artifact.%s' % (name),
          timeout_seconds=5*60,
          flake_retries=flake_retries,
          timeout_retries=timeout_retries,
          shell=shell)
  return jobspec


def macos_arch_env(arch):
  """Returns environ specifying -arch arguments for make."""
  if arch == 'x86':
    arch_arg = '-arch i386'
  elif arch == 'x64':
    arch_arg = '-arch x86_64'
  else:
    raise Exception('Unsupported arch')
  return {'CFLAGS': arch_arg, 'LDFLAGS': arch_arg}


class CSharpExtArtifact:
  """Builds C# native extension library"""

  def __init__(self, platform, arch):
    self.name = 'csharp_ext_%s_%s' % (platform, arch)
    self.platform = platform
    self.arch = arch
    self.labels = ['csharp', platform, arch]

  def pre_build_jobspecs(self):
    if self.platform == 'windows':
      return [create_jobspec('prebuild_%s' % self.name,
                             ['tools\\run_tests\\pre_build_c.bat'],
                             shell=True,
                             flake_retries=5,
                             timeout_retries=2)]
    else:
      return []

  def build_jobspec(self):
    if self.platform == 'windows':
      msbuild_platform = 'Win32' if self.arch == 'x86' else self.arch
      return create_jobspec(self.name,
                            ['tools\\run_tests\\build_artifact_csharp.bat',
                             'vsprojects\\grpc_csharp_ext.sln',
                             '/p:Configuration=Release',
                             '/p:PlatformToolset=v120',
                             '/p:Platform=%s' % msbuild_platform],
                            shell=True)
    else:
      environ = {'CONFIG': 'opt',
                 'EMBED_OPENSSL': 'true',
                 'EMBED_ZLIB': 'true'}
      if self.platform == 'linux':
        return create_docker_jobspec(self.name,
            'tools/jenkins/grpc_artifact_linux_%s' % self.arch,
            'tools/run_tests/build_artifact_csharp.sh')
      else:
        environ.update(macos_arch_env(self.arch))
        return create_jobspec(self.name,
                              ['tools/run_tests/build_artifact_csharp.sh'],
                              environ=environ)

  def __str__(self):
    return self.name


_ARTIFACTS = [
    CSharpExtArtifact('linux', 'x86'),
    CSharpExtArtifact('linux', 'x64'),
    CSharpExtArtifact('macos', 'x86'),
    CSharpExtArtifact('macos', 'x64'),
    CSharpExtArtifact('windows', 'x86'),
    CSharpExtArtifact('windows', 'x64')
]


def _create_build_map():
  """Maps artifact names and labels to list of artifacts to be built."""
  artifact_build_map = dict([(artifact.name, [artifact])
                             for artifact in _ARTIFACTS])
  if len(_ARTIFACTS) > len(artifact_build_map.keys()):
    raise Exception('Artifact names need to be unique')

  label_build_map = {}
  label_build_map['all'] = [a for a in _ARTIFACTS]  # to build all artifacts
  for artifact in _ARTIFACTS:
    for label in artifact.labels:
      if label in label_build_map:
        label_build_map[label].append(artifact)
      else:
        label_build_map[label] = [artifact]

  if set(artifact_build_map.keys()).intersection(label_build_map.keys()):
    raise Exception('Artifact names need to be distinct from label names')
  return dict( artifact_build_map.items() + label_build_map.items())


_BUILD_MAP = _create_build_map()

argp = argparse.ArgumentParser(description='Builds distribution artifacts.')
argp.add_argument('-b', '--build',
                  choices=sorted(_BUILD_MAP.keys()),
                  nargs='+',
                  default=['all'],
                  help='Artifact name or artifact label to build.')
argp.add_argument('-f', '--filter',
                  choices=sorted(_BUILD_MAP.keys()),
                  nargs='+',
                  default=[],
                  help='Filter artifacts to build with AND semantics.')
argp.add_argument('-j', '--jobs', default=multiprocessing.cpu_count(), type=int)
argp.add_argument('-t', '--travis',
                  default=False,
                  action='store_const',
                  const=True)

args = argp.parse_args()

# Figure out which artifacts to build
artifacts = []
for label in args.build:
  artifacts += _BUILD_MAP[label]

# Among target selected by -b, filter out those that don't match the filter
artifacts = [a for a in artifacts if all(f in a.labels for f in args.filter)]
artifacts = sorted(set(artifacts))

# Execute pre-build phase
prebuild_jobs = []
for artifact in artifacts:
  prebuild_jobs += artifact.pre_build_jobspecs()
if prebuild_jobs:
  num_failures, _ = jobset.run(
    prebuild_jobs, newline_on_success=True, maxjobs=args.jobs)
  if num_failures != 0:
    jobset.message('FAILED', 'Pre-build phase failed.', do_newline=True)
    sys.exit(1)

build_jobs = []
for artifact in artifacts:
  build_jobs.append(artifact.build_jobspec())
if not build_jobs:
  print 'Nothing to build.'
  sys.exit(1)

jobset.message('START', 'Building artifacts.', do_newline=True)
num_failures, _ = jobset.run(
    build_jobs, newline_on_success=True, maxjobs=args.jobs)
if num_failures == 0:
  jobset.message('SUCCESS', 'All artifacts built successfully.',
                 do_newline=True)
else:
  jobset.message('FAILED', 'Failed to build artifacts.',
                 do_newline=True)
  sys.exit(1)
