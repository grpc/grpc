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

from distutils import extension
import os
import os.path
import shlex
import sys

import setuptools
from setuptools.command import build_ext

# TODO(atash) add flag to disable Cython use

os.chdir(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.abspath('.'))

# There are some situations (like on Windows) where CC, CFLAGS, and LDFLAGS are
# entirely ignored/dropped/forgotten by distutils and its Cygwin/MinGW support.
# We use these environment variables to thus get around that without locking
# ourselves in w.r.t. the multitude of operating systems this ought to build on.
# By default we assume a GCC-like compiler.
EXTRA_COMPILE_ARGS = shlex.split(os.environ.get('GRPC_PYTHON_CFLAGS',
                                                '-frtti -std=c++11'))
EXTRA_LINK_ARGS = shlex.split(os.environ.get('GRPC_PYTHON_LDFLAGS',
                                             '-lpthread'))

import protoc_lib_deps
import grpc_version

def protoc_ext_module():
  plugin_sources = [
      'grpc/tools/main.cc',
      'grpc_root/src/compiler/python_generator.cc'] + [
      os.path.join('third_party/protobuf/src', cc_file)
      for cc_file in protoc_lib_deps.CC_FILES]
  plugin_ext = extension.Extension(
      name='grpc.tools.protoc_compiler',
      sources=['grpc/tools/protoc_compiler.pyx'] + plugin_sources,
      include_dirs=[
          '.',
          'grpc_root',
          'grpc_root/include',
          'third_party/protobuf/src',
      ],
      language='c++',
      define_macros=[('HAVE_PTHREAD', 1)],
      extra_compile_args=EXTRA_COMPILE_ARGS,
      extra_link_args=EXTRA_LINK_ARGS,
  )
  return plugin_ext

def maybe_cythonize(exts):
  from Cython import Build
  return Build.cythonize(exts)

setuptools.setup(
  name='grpcio_tools',
  version=grpc_version.VERSION,
  license='3-clause BSD',
  ext_modules=maybe_cythonize([
      protoc_ext_module(),
  ]),
  packages=setuptools.find_packages('.'),
  # TODO(atash): Figure out why auditwheel doesn't like namespace packages.
  #namespace_packages=['grpc'],
  install_requires=[
    'protobuf>=3.0.0a3',
  ],
)
