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

import os
import os.path
import sys

import setuptools

_CONF_PY_ADDENDUM = """
extensions.append('sphinx.ext.napoleon')
napoleon_google_docstring = True
napoleon_numpy_docstring = True

html_theme = 'sphinx_rtd_theme'
"""


class SphinxDocumentation(setuptools.Command):
  """Command to generate documentation via sphinx."""

  description = ''
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
        os.getcwd(), self.distribution.package_dir['grpc'])
    sys.path.append(src_dir)
    sphinx.apidoc.main([
        '', '--force', '--full', '-H', metadata.name, '-A', metadata.author,
        '-V', metadata.version, '-R', metadata.version,
        '-o', os.path.join('doc', 'src'), src_dir])
    conf_filepath = os.path.join('doc', 'src', 'conf.py')
    with open(conf_filepath, 'a') as conf_file:
      conf_file.write(_CONF_PY_ADDENDUM)
    sphinx.main(['', os.path.join('doc', 'src'), os.path.join('doc', 'build')])

