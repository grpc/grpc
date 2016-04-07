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

import os
import platform
import shutil
import sys
import sysconfig

import setuptools

import commands
import grpc_version

try:
  from urllib2 import urlopen
except ImportError:
  from urllib.request import urlopen

PYTHON_STEM = os.path.dirname(os.path.abspath(__file__))
BINARIES_REPOSITORY = os.environ.get(
    'GRPC_PYTHON_BINARIES_REPOSITORY',
    'https://storage.googleapis.com/grpc-precompiled-binaries/python')
USE_PRECOMPILED_BINARIES = bool(int(os.environ.get(
    'GRPC_PYTHON_USE_PRECOMPILED_BINARIES', '1')))

def _tagged_ext_name(base):
  uname = platform.uname()
  tags = (
      grpc_version.VERSION,
      'py{}'.format(sysconfig.get_python_version()),
      uname[0],
      uname[4],
  )
  ucs = 'ucs{}'.format(sysconfig.get_config_var('Py_UNICODE_SIZE'))
  return '{base}-{tags}-{ucs}'.format(
      base=base, tags='-'.join(tags), ucs=ucs)


class BuildTaggedExt(setuptools.Command):

  description = 'build the gRPC tagged extensions'
  user_options = []

  def initialize_options(self):
    # distutils requires this override.
    pass

  def finalize_options(self):
    # distutils requires this override.
    pass

  def run(self):
    if 'linux' in sys.platform:
      self.run_command('build_ext')
      try:
        os.makedirs('dist/')
      except OSError:
        pass
      shutil.copyfile(
          os.path.join(PYTHON_STEM, 'grpc/_cython/cygrpc.so'),
          'dist/{}.so'.format(_tagged_ext_name('cygrpc')))
    else:
      sys.stderr.write('nothing to do for build_tagged_ext\n')


def update_setup_arguments(setup_arguments):
  if not USE_PRECOMPILED_BINARIES:
    sys.stderr.write('not using precompiled extension')
    return
  url = '{}/{}.so'.format(BINARIES_REPOSITORY, _tagged_ext_name('cygrpc'))
  target_path = os.path.join(PYTHON_STEM, 'grpc/_cython/cygrpc.so')
  try:
    extension = urlopen(url).read()
  except:
    sys.stderr.write(
        'could not download precompiled extension: {}\n'.format(url))
    return
  try:
    with open(target_path, 'w') as target:
      target.write(extension)
    setup_arguments['ext_modules'] = []
  except:
    sys.stderr.write(
        'could not write precompiled extension to directory: {} -> {}\n'
            .format(url, target_path))
    return
  setup_arguments['package_data']['grpc._cython'].append('cygrpc.so')
