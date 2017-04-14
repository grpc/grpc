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

"""Definition of targets to build artifacts."""

import os.path
import random
import string
import sys

sys.path.insert(0, os.path.abspath('..'))
import python_utils.jobset as jobset


def create_docker_jobspec(name, dockerfile_dir, shell_command, environ={},
                   flake_retries=0, timeout_retries=0, timeout_seconds=30*60,
                   docker_base_image=None):
  """Creates jobspec for a task running under docker."""
  environ = environ.copy()
  environ['RUN_COMMAND'] = shell_command

  docker_args=[]
  for k,v in environ.items():
    docker_args += ['-e', '%s=%s' % (k, v)]
  docker_env = {'DOCKERFILE_DIR': dockerfile_dir,
                'DOCKER_RUN_SCRIPT': 'tools/run_tests/dockerize/docker_run.sh',
                'OUTPUT_DIR': 'artifacts'}

  if docker_base_image is not None:
    docker_env['DOCKER_BASE_IMAGE'] = docker_base_image
  jobspec = jobset.JobSpec(
          cmdline=['tools/run_tests/dockerize/build_and_run_docker.sh'] + docker_args,
          environ=docker_env,
          shortname='build_artifact.%s' % (name),
          timeout_seconds=timeout_seconds,
          flake_retries=flake_retries,
          timeout_retries=timeout_retries)
  return jobspec


def create_jobspec(name, cmdline, environ=None, shell=False,
                   flake_retries=0, timeout_retries=0, timeout_seconds=30*60):
  """Creates jobspec."""
  jobspec = jobset.JobSpec(
          cmdline=cmdline,
          environ=environ,
          shortname='build_artifact.%s' % (name),
          timeout_seconds=timeout_seconds,
          flake_retries=flake_retries,
          timeout_retries=timeout_retries,
          shell=shell)
  return jobspec


_MACOS_COMPAT_FLAG = '-mmacosx-version-min=10.7'

_ARCH_FLAG_MAP = {
  'x86': '-m32',
  'x64': '-m64'
}


class PythonArtifact:
  """Builds Python artifacts."""

  def __init__(self, platform, arch, py_version):
    self.name = 'python_%s_%s_%s' % (platform, arch, py_version)
    self.platform = platform
    self.arch = arch
    self.labels = ['artifact', 'python', platform, arch, py_version]
    self.py_version = py_version

  def pre_build_jobspecs(self):
    return []

  def build_jobspec(self):
    environ = {}
    if self.platform == 'linux':
      if self.arch == 'x86':
        environ['SETARCH_CMD'] = 'linux32'
      # Inside the manylinux container, the python installations are located in
      # special places...
      environ['PYTHON'] = '/opt/python/{}/bin/python'.format(self.py_version)
      environ['PIP'] = '/opt/python/{}/bin/pip'.format(self.py_version)
      # Platform autodetection for the manylinux1 image breaks so we set the
      # defines ourselves.
      # TODO(atash) get better platform-detection support in core so we don't
      # need to do this manually...
      environ['CFLAGS'] = '-DGPR_MANYLINUX1=1'
      environ['GRPC_BUILD_GRPCIO_TOOLS_DEPENDENTS'] = 'TRUE'
      environ['GRPC_BUILD_MANYLINUX_WHEEL'] = 'TRUE'
      return create_docker_jobspec(self.name,
          'tools/dockerfile/grpc_artifact_python_manylinux_%s' % self.arch,
          'tools/run_tests/artifacts/build_artifact_python.sh',
          environ=environ,
          timeout_seconds=60*60,
          docker_base_image='quay.io/pypa/manylinux1_i686' if self.arch == 'x86' else 'quay.io/pypa/manylinux1_x86_64')
    elif self.platform == 'windows':
      if 'Python27' in self.py_version or 'Python34' in self.py_version:
        environ['EXT_COMPILER'] = 'mingw32'
      else:
        environ['EXT_COMPILER'] = 'msvc'
      # For some reason, the batch script %random% always runs with the same
      # seed.  We create a random temp-dir here
      dir = ''.join(random.choice(string.ascii_uppercase) for _ in range(10))
      return create_jobspec(self.name,
                            ['tools\\run_tests\\artifacts\\build_artifact_python.bat',
                             self.py_version,
                             '32' if self.arch == 'x86' else '64',
                             dir
                            ],
                            environ=environ,
                            shell=True)
    else:
      environ['PYTHON'] = self.py_version
      environ['SKIP_PIP_INSTALL'] = 'TRUE'
      return create_jobspec(self.name,
                            ['tools/run_tests/artifacts/build_artifact_python.sh'],
                            environ=environ)

  def __str__(self):
    return self.name


class RubyArtifact:
  """Builds ruby native gem."""

  def __init__(self, platform, arch):
    self.name = 'ruby_native_gem_%s_%s' % (platform, arch)
    self.platform = platform
    self.arch = arch
    self.labels = ['artifact', 'ruby', platform, arch]

  def pre_build_jobspecs(self):
    return []

  def build_jobspec(self):
    if self.platform == 'windows':
      raise Exception("Not supported yet")
    else:
      if self.platform == 'linux':
        environ = {}
        if self.arch == 'x86':
          environ['SETARCH_CMD'] = 'linux32'
        return create_docker_jobspec(self.name,
            'tools/dockerfile/grpc_artifact_linux_%s' % self.arch,
            'tools/run_tests/artifacts/build_artifact_ruby.sh',
            environ=environ)
      else:
        return create_jobspec(self.name,
                              ['tools/run_tests/artifacts/build_artifact_ruby.sh'])


class CSharpExtArtifact:
  """Builds C# native extension library"""

  def __init__(self, platform, arch):
    self.name = 'csharp_ext_%s_%s' % (platform, arch)
    self.platform = platform
    self.arch = arch
    self.labels = ['artifact', 'csharp', platform, arch]

  def pre_build_jobspecs(self):
    return []

  def build_jobspec(self):
    if self.platform == 'windows':
      cmake_arch_option = 'Win32' if self.arch == 'x86' else self.arch
      return create_jobspec(self.name,
                            ['tools\\run_tests\\artifacts\\build_artifact_csharp.bat',
                             cmake_arch_option],
                            shell=True)
    else:
      environ = {'CONFIG': 'opt',
                 'EMBED_OPENSSL': 'true',
                 'EMBED_ZLIB': 'true',
                 'CFLAGS': '-DGPR_BACKWARDS_COMPATIBILITY_MODE',
                 'LDFLAGS': ''}
      if self.platform == 'linux':
        return create_docker_jobspec(self.name,
            'tools/dockerfile/grpc_artifact_linux_%s' % self.arch,
            'tools/run_tests/artifacts/build_artifact_csharp.sh',
            environ=environ)
      else:
        archflag = _ARCH_FLAG_MAP[self.arch]
        environ['CFLAGS'] += ' %s %s' % (archflag, _MACOS_COMPAT_FLAG)
        environ['LDFLAGS'] += ' %s' % archflag
        return create_jobspec(self.name,
                              ['tools/run_tests/artifacts/build_artifact_csharp.sh'],
                              environ=environ)

  def __str__(self):
    return self.name


node_gyp_arch_map = {
  'x86': 'ia32',
  'x64': 'x64'
}

class NodeExtArtifact:
  """Builds Node native extension"""

  def __init__(self, platform, arch):
    self.name = 'node_ext_{0}_{1}'.format(platform, arch)
    self.platform = platform
    self.arch = arch
    self.gyp_arch = node_gyp_arch_map[arch]
    self.labels = ['artifact', 'node', platform, arch]

  def pre_build_jobspecs(self):
    return []

  def build_jobspec(self):
    if self.platform == 'windows':
      return create_jobspec(self.name,
                            ['tools\\run_tests\\artifacts\\build_artifact_node.bat',
                             self.gyp_arch],
                            shell=True)
    else:
      if self.platform == 'linux':
        return create_docker_jobspec(
            self.name,
            'tools/dockerfile/grpc_artifact_linux_{}'.format(self.arch),
            'tools/run_tests/artifacts/build_artifact_node.sh {}'.format(self.gyp_arch))
      else:
        return create_jobspec(self.name,
                              ['tools/run_tests/artifacts/build_artifact_node.sh',
                               self.gyp_arch])

class PHPArtifact:
  """Builds PHP PECL package"""

  def __init__(self, platform, arch):
    self.name = 'php_pecl_package_{0}_{1}'.format(platform, arch)
    self.platform = platform
    self.arch = arch
    self.labels = ['artifact', 'php', platform, arch]

  def pre_build_jobspecs(self):
    return []

  def build_jobspec(self):
    if self.platform == 'linux':
      return create_docker_jobspec(
          self.name,
          'tools/dockerfile/grpc_artifact_linux_{}'.format(self.arch),
          'tools/run_tests/artifacts/build_artifact_php.sh')
    else:
      return create_jobspec(self.name,
                            ['tools/run_tests/artifacts/build_artifact_php.sh'])

class ProtocArtifact:
  """Builds protoc and protoc-plugin artifacts"""

  def __init__(self, platform, arch):
    self.name = 'protoc_%s_%s' % (platform, arch)
    self.platform = platform
    self.arch = arch
    self.labels = ['artifact', 'protoc', platform, arch]

  def pre_build_jobspecs(self):
      return []

  def build_jobspec(self):
    if self.platform != 'windows':
      cxxflags = '-DNDEBUG %s' % _ARCH_FLAG_MAP[self.arch]
      ldflags = '%s' % _ARCH_FLAG_MAP[self.arch]
      if self.platform != 'macos':
        ldflags += '  -static-libgcc -static-libstdc++ -s'
      environ={'CONFIG': 'opt',
               'CXXFLAGS': cxxflags,
               'LDFLAGS': ldflags,
               'PROTOBUF_LDFLAGS_EXTRA': ldflags}
      if self.platform == 'linux':
        return create_docker_jobspec(self.name,
            'tools/dockerfile/grpc_artifact_protoc',
            'tools/run_tests/artifacts/build_artifact_protoc.sh',
            environ=environ)
      else:
        environ['CXXFLAGS'] += ' -std=c++11 -stdlib=libc++ %s' % _MACOS_COMPAT_FLAG
        return create_jobspec(self.name,
            ['tools/run_tests/artifacts/build_artifact_protoc.sh'],
            environ=environ)
    else:
      generator = 'Visual Studio 12 Win64' if self.arch == 'x64' else 'Visual Studio 12' 
      vcplatform = 'x64' if self.arch == 'x64' else 'Win32'
      return create_jobspec(self.name,
                            ['tools\\run_tests\\artifacts\\build_artifact_protoc.bat'],
                            environ={'generator': generator,
                                     'Platform': vcplatform})

  def __str__(self):
    return self.name


def targets():
  """Gets list of supported targets"""
  return ([Cls(platform, arch)
           for Cls in (CSharpExtArtifact, NodeExtArtifact, ProtocArtifact)
           for platform in ('linux', 'macos', 'windows')
           for arch in ('x86', 'x64')] +
          [PythonArtifact('linux', 'x86', 'cp27-cp27m'),
           PythonArtifact('linux', 'x86', 'cp27-cp27mu'),
           PythonArtifact('linux', 'x86', 'cp34-cp34m'),
           PythonArtifact('linux', 'x86', 'cp35-cp35m'),
           PythonArtifact('linux', 'x86', 'cp36-cp36m'),
           PythonArtifact('linux', 'x64', 'cp27-cp27m'),
           PythonArtifact('linux', 'x64', 'cp27-cp27mu'),
           PythonArtifact('linux', 'x64', 'cp34-cp34m'),
           PythonArtifact('linux', 'x64', 'cp35-cp35m'),
           PythonArtifact('linux', 'x64', 'cp36-cp36m'),
           PythonArtifact('macos', 'x64', 'python2.7'),
           PythonArtifact('macos', 'x64', 'python3.4'),
           PythonArtifact('macos', 'x64', 'python3.5'),
           PythonArtifact('macos', 'x64', 'python3.6'),
           PythonArtifact('windows', 'x86', 'Python27_32bits'),
           PythonArtifact('windows', 'x86', 'Python34_32bits'),
           PythonArtifact('windows', 'x86', 'Python35_32bits'),
           PythonArtifact('windows', 'x86', 'Python36_32bits'),
           PythonArtifact('windows', 'x64', 'Python27'),
           PythonArtifact('windows', 'x64', 'Python34'),
           PythonArtifact('windows', 'x64', 'Python35'),
           PythonArtifact('windows', 'x64', 'Python36'),
           RubyArtifact('linux', 'x86'),
           RubyArtifact('linux', 'x64'),
           RubyArtifact('macos', 'x64'),
           PHPArtifact('linux', 'x64'),
           PHPArtifact('macos', 'x64')])
