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
import platform
import re
import shutil
import subprocess
import sys
import traceback

import setuptools
from setuptools.command import build_ext
from setuptools.command import build_py
from setuptools.command import easy_install
from setuptools.command import install
from setuptools.command import test

import support

PYTHON_STEM = os.path.dirname(os.path.abspath(__file__))
GRPC_STEM = os.path.abspath(PYTHON_STEM + '../../../../')
PROTO_STEM = os.path.join(GRPC_STEM, 'src', 'proto')
PROTO_GEN_STEM = os.path.join(GRPC_STEM, 'src', 'python', 'gens')

CONF_PY_ADDENDUM = """
extensions.append('sphinx.ext.napoleon')
napoleon_google_docstring = True
napoleon_numpy_docstring = True

html_theme = 'sphinx_rtd_theme'
"""


class CommandError(Exception):
  """Simple exception class for GRPC custom commands."""


# TODO(atash): Remove this once PyPI has better Linux bdist support. See
# https://bitbucket.org/pypa/pypi/issues/120/binary-wheels-for-linux-are-not-supported
def _get_grpc_custom_bdist(decorated_basename, target_bdist_basename):
  """Returns a string path to a bdist file for Linux to install.

  If we can retrieve a pre-compiled bdist from online, uses it. Else, emits a
  warning and builds from source.
  """
  # TODO(atash): somehow the name that's returned from `wheel` is different
  # between different versions of 'wheel' (but from a compatibility standpoint,
  # the names are compatible); we should have some way of determining name
  # compatibility in the same way `wheel` does to avoid having to rename all of
  # the custom wheels that we build/upload to GCS.

  # Break import style to ensure that setup.py has had a chance to install the
  # relevant package.
  from six.moves.urllib import request
  decorated_path = decorated_basename + GRPC_CUSTOM_BDIST_EXT
  try:
    url = BINARIES_REPOSITORY + '/{target}'.format(target=decorated_path)
    bdist_data = request.urlopen(url).read()
  except IOError as error:
    raise CommandError(
        '{}\n\nCould not find the bdist {}: {}'
            .format(traceback.format_exc(), decorated_path, error.message))
  # Our chosen local bdist path.
  bdist_path = target_bdist_basename + GRPC_CUSTOM_BDIST_EXT
  try:
    with open(bdist_path, 'w') as bdist_file:
      bdist_file.write(bdist_data)
  except IOError as error:
    raise CommandError(
        '{}\n\nCould not write grpcio bdist: {}'
            .format(traceback.format_exc(), error.message))
  return bdist_path


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
    src_dir = os.path.join(PYTHON_STEM, 'grpc')
    sys.path.append(src_dir)
    sphinx.apidoc.main([
        '', '--force', '--full', '-H', metadata.name, '-A', metadata.author,
        '-V', metadata.version, '-R', metadata.version,
        '-o', os.path.join('doc', 'src'), src_dir])
    conf_filepath = os.path.join('doc', 'src', 'conf.py')
    with open(conf_filepath, 'a') as conf_file:
      conf_file.write(CONF_PY_ADDENDUM)
    sphinx.main(['', os.path.join('doc', 'src'), os.path.join('doc', 'build')])


class BuildProjectMetadata(setuptools.Command):
  """Command to generate project metadata in a module."""

  description = 'build grpcio project metadata files'
  user_options = []

  def initialize_options(self):
    pass

  def finalize_options(self):
    pass

  def run(self):
    with open(os.path.join(PYTHON_STEM, 'grpc/_grpcio_metadata.py'), 'w') as module_file:
      module_file.write('__version__ = """{}"""'.format(
          self.distribution.get_version()))


class BuildPy(build_py.build_py):
  """Custom project build command."""

  def run(self):
    self.run_command('build_project_metadata')
    build_py.build_py.run(self)


class BuildExt(build_ext.build_ext):
  """Custom build_ext command to enable compiler-specific flags."""

  C_OPTIONS = {
      'unix': ('-pthread', '-std=gnu99'),
      'msvc': (),
  }
  LINK_OPTIONS = {}

  def build_extensions(self):
    compiler = self.compiler.compiler_type
    if compiler in BuildExt.C_OPTIONS:
      for extension in self.extensions:
        extension.extra_compile_args += list(BuildExt.C_OPTIONS[compiler])
    if compiler in BuildExt.LINK_OPTIONS:
      for extension in self.extensions:
        extension.extra_link_args += list(BuildExt.LINK_OPTIONS[compiler])
    try:
      build_ext.build_ext.build_extensions(self)
    except Exception as error:
      formatted_exception = traceback.format_exc()
      support.diagnose_build_ext_error(self, error, formatted_exception)
      raise CommandError(
          "Failed `build_ext` step:\n{}".format(formatted_exception))


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
