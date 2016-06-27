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

"""Provides distutils command classes for the GRPC Python setup process."""

import distutils
import glob
import os
import os.path
import shutil
import subprocess
import sys

import setuptools
from setuptools.command import build_py
from setuptools.command import sdist

ROOT_DIR = os.path.abspath(os.path.dirname(os.path.abspath(__file__)))
HEALTH_PROTO = os.path.join(ROOT_DIR, '../../proto/grpc/health/v1/health.proto')


class BuildProtoModules(setuptools.Command):
  """Command to generate project *_pb2.py modules from proto files."""

  description = ''
  user_options = []

  def initialize_options(self):
    pass

  def finalize_options(self):
    self.protoc_command = distutils.spawn.find_executable('protoc')
    self.grpc_python_plugin_command = distutils.spawn.find_executable(
        'grpc_python_plugin')

  def run(self):
    paths = []
    root_directory = os.getcwd()
    for walk_root, directories, filenames in os.walk(root_directory):
      for filename in filenames:
        if filename.endswith('.proto'):
          paths.append(os.path.join(walk_root, filename))
    command = [
        self.protoc_command,
        '--plugin=protoc-gen-python-grpc={}'.format(
            self.grpc_python_plugin_command),
        '-I {}'.format(root_directory),
        '--python_out={}'.format(root_directory),
        '--python-grpc_out={}'.format(root_directory),
    ] + paths
    try:
      subprocess.check_output(' '.join(command), cwd=root_directory, shell=True,
                              stderr=subprocess.STDOUT)
    except subprocess.CalledProcessError as e:
      raise Exception('{}\nOutput:\n{}'.format(e.message, e.output))


class CopyProtoModules(setuptools.Command):
  """Command to copy proto modules from grpc/src/proto."""

  def initialize_options(self):
    pass

  def finalize_options(self):
    pass

  def run(self):
    if os.path.isfile(HEALTH_PROTO):
      shutil.copyfile(
          HEALTH_PROTO,
          os.path.join(ROOT_DIR, 'grpc_health/health/v1/health.proto'))


class BuildPy(build_py.build_py):
  """Custom project build command."""

  def run(self):
    self.run_command('copy_proto_modules')
    self.run_command('build_proto_modules')
    build_py.build_py.run(self)


class SDist(sdist.sdist):
  """Custom project build command."""

  def run(self):
    self.run_command('copy_proto_modules')
    sdist.sdist.run(self)
