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
import os
import os.path
import re
import subprocess
import sys

import setuptools
from setuptools.command import build_py
from setuptools.command import test

CONF_PY_ADDENDUM = """
extensions.append('sphinx.ext.napoleon')
napoleon_google_docstring = True
napoleon_numpy_docstring = True

html_theme = 'sphinx_rtd_theme'
"""


class SphinxDocumentation(setuptools.Command):
  """Command to generate documentation via sphinx."""

  description = 'generate sphinx documentation'
  user_options = []

  def initialize_options(self):
    pass

  def finalize_options(self):
    pass

  def run(self):
    # We import here to ensure that setup.py has had a chance to install the
    # relevant package eggs first.
    import sphinx
    import sphinx.apidoc
    metadata = self.distribution.metadata
    src_dir = os.path.join(
        os.getcwd(), self.distribution.package_dir[''], 'grpc')
    sys.path.append(src_dir)
    sphinx.apidoc.main([
        '', '--force', '--full', '-H', metadata.name, '-A', metadata.author,
        '-V', metadata.version, '-R', metadata.version,
        '-o', os.path.join('doc', 'src'), src_dir])
    conf_filepath = os.path.join('doc', 'src', 'conf.py')
    with open(conf_filepath, 'a') as conf_file:
      conf_file.write(CONF_PY_ADDENDUM)
    sphinx.main(['', os.path.join('doc', 'src'), os.path.join('doc', 'build')])


class BuildProtoModules(setuptools.Command):
  """Command to generate project *_pb2.py modules from proto files."""

  description = 'build protobuf modules'
  user_options = [
    ('include=', None, 'path patterns to include in protobuf generation'),
    ('exclude=', None, 'path patterns to exclude from protobuf generation')
  ]

  def initialize_options(self):
    self.exclude = None
    self.include = r'.*\.proto$'
    self.protoc_command = None
    self.grpc_python_plugin_command = None

  def finalize_options(self):
    self.protoc_command = distutils.spawn.find_executable('protoc')
    self.grpc_python_plugin_command = distutils.spawn.find_executable(
        'grpc_python_plugin')

  def run(self):
    include_regex = re.compile(self.include)
    exclude_regex = re.compile(self.exclude) if self.exclude else None
    paths = []
    root_directory = os.getcwd()
    for walk_root, directories, filenames in os.walk(root_directory):
      for filename in filenames:
        path = os.path.join(walk_root, filename)
        if include_regex.match(path) and not (
            exclude_regex and exclude_regex.match(path)):
          paths.append(path)
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
      raise Exception('Command:\n{}\nMessage:\n{}\nOutput:\n{}'.format(
          command, e.message, e.output))


class BuildProjectMetadata(setuptools.Command):
  """Command to generate project metadata in a module."""

  description = 'build grpcio project metadata files'
  user_options = []

  def initialize_options(self):
    pass

  def finalize_options(self):
    pass

  def run(self):
    with open('grpc/_grpcio_metadata.py', 'w') as module_file:
      module_file.write('__version__ = """{}"""'.format(
          self.distribution.get_version()))


class BuildPy(build_py.build_py):
  """Custom project build command."""

  def run(self):
    self.run_command('build_proto_modules')
    self.run_command('build_project_metadata')
    build_py.build_py.run(self)


class Gather(setuptools.Command):
  """Command to gather project dependencies."""

  description = 'gather dependencies for grpcio'
  user_options = [
    ('test', 't', 'flag indicating to gather test dependencies'),
    ('install', 'i', 'flag indicating to gather install dependencies')
  ]

  def initialize_options(self):
    self.test = False
    self.install = False

  def finalize_options(self):
    # distutils requires this override.
    pass

  def run(self):
    if self.install and self.distribution.install_requires:
      self.distribution.fetch_build_eggs(self.distribution.install_requires)
    if self.test and self.distribution.tests_require:
      self.distribution.fetch_build_eggs(self.distribution.tests_require)


class RunInterop(test.test):

  description = 'run interop test client/server'
  user_options = [
    ('args=', 'a', 'pass-thru arguments for the client/server'),
    ('client', 'c', 'flag indicating to run the client'),
    ('server', 's', 'flag indicating to run the server')
  ]

  def initialize_options(self):
    self.args = ''
    self.client = False
    self.server = False

  def finalize_options(self):
    if self.client and self.server:
      raise DistutilsOptionError('you may only specify one of client or server')

  def run(self):
    if self.distribution.install_requires:
      self.distribution.fetch_build_eggs(self.distribution.install_requires)
    if self.distribution.tests_require:
      self.distribution.fetch_build_eggs(self.distribution.tests_require)
    if self.client:
      self.run_client()
    elif self.server:
      self.run_server()

  def run_server(self):
    # We import here to ensure that our setuptools parent has had a chance to
    # edit the Python system path.
    from tests.interop import server
    sys.argv[1:] = self.args.split()
    server.serve()

  def run_client(self):
    # We import here to ensure that our setuptools parent has had a chance to
    # edit the Python system path.
    from tests.interop import client
    sys.argv[1:] = self.args.split()
    client.test_interoperability()
