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

from distutils import cygwinccompiler
from distutils import extension
from distutils import util
import errno
import os
import os.path
import pkg_resources
import platform
import re
import shlex
import shutil
import sys
import sysconfig

import setuptools
from setuptools.command import build_ext

# TODO(atash) add flag to disable Cython use

os.chdir(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.abspath('.'))

import protoc_lib_deps
import grpc_version

PY3 = sys.version_info.major == 3

# There are some situations (like on Windows) where CC, CFLAGS, and LDFLAGS are
# entirely ignored/dropped/forgotten by distutils and its Cygwin/MinGW support.
# We use these environment variables to thus get around that without locking
# ourselves in w.r.t. the multitude of operating systems this ought to build on.
# We can also use these variables as a way to inject environment-specific
# compiler/linker flags. We assume GCC-like compilers and/or MinGW as a
# reasonable default.
EXTRA_ENV_COMPILE_ARGS = os.environ.get('GRPC_PYTHON_CFLAGS', None)
EXTRA_ENV_LINK_ARGS = os.environ.get('GRPC_PYTHON_LDFLAGS', None)
if EXTRA_ENV_COMPILE_ARGS is None:
  EXTRA_ENV_COMPILE_ARGS = '-std=c++11'
  if 'win32' in sys.platform:
    if sys.version_info < (3, 5):
      # We use define flags here and don't directly add to DEFINE_MACROS below to
      # ensure that the expert user/builder has a way of turning it off (via the
      # envvars) without adding yet more GRPC-specific envvars.
      # See https://sourceforge.net/p/mingw-w64/bugs/363/
      if '32' in platform.architecture()[0]:
        EXTRA_ENV_COMPILE_ARGS += ' -D_ftime=_ftime32 -D_timeb=__timeb32 -D_ftime_s=_ftime32_s'
      else:
        EXTRA_ENV_COMPILE_ARGS += ' -D_ftime=_ftime64 -D_timeb=__timeb64'
    else:
      # We need to statically link the C++ Runtime, only the C runtime is
      # available dynamically
      EXTRA_ENV_COMPILE_ARGS += ' /MT'
  elif "linux" in sys.platform or "darwin" in sys.platform:
    EXTRA_ENV_COMPILE_ARGS += ' -fno-wrapv -frtti'
if EXTRA_ENV_LINK_ARGS is None:
  EXTRA_ENV_LINK_ARGS = ''
  if "linux" in sys.platform or "darwin" in sys.platform:
    EXTRA_ENV_LINK_ARGS += ' -lpthread'
  elif "win32" in sys.platform and sys.version_info < (3, 5):
    msvcr = cygwinccompiler.get_msvcr()[0]
    # TODO(atash) sift through the GCC specs to see if libstdc++ can have any
    # influence on the linkage outcome on MinGW for non-C++ programs.
    EXTRA_ENV_LINK_ARGS += (
        ' -static-libgcc -static-libstdc++ -mcrtdll={msvcr} '
        '-static'.format(msvcr=msvcr))

EXTRA_COMPILE_ARGS = shlex.split(EXTRA_ENV_COMPILE_ARGS)
EXTRA_LINK_ARGS = shlex.split(EXTRA_ENV_LINK_ARGS)

CC_FILES = [
  os.path.normpath(cc_file) for cc_file in protoc_lib_deps.CC_FILES]
PROTO_FILES = [
  os.path.normpath(proto_file) for proto_file in protoc_lib_deps.PROTO_FILES]
CC_INCLUDE = os.path.normpath(protoc_lib_deps.CC_INCLUDE)
PROTO_INCLUDE = os.path.normpath(protoc_lib_deps.PROTO_INCLUDE)

GRPC_PYTHON_TOOLS_PACKAGE = 'grpc.tools'
GRPC_PYTHON_PROTO_RESOURCES_NAME = '_proto'

DEFINE_MACROS = ()
if "win32" in sys.platform:
  DEFINE_MACROS += (('WIN32_LEAN_AND_MEAN', 1),)
  if '64bit' in platform.architecture()[0]:
    DEFINE_MACROS += (('MS_WIN64', 1),)
elif "linux" in sys.platform or "darwin" in sys.platform:
  DEFINE_MACROS += (('HAVE_PTHREAD', 1),)

# By default, Python3 distutils enforces compatibility of
# c plugins (.so files) with the OSX version Python3 was built with.
# For Python3.4, this is OSX 10.6, but we need Thread Local Support (__thread)
if 'darwin' in sys.platform and PY3:
  mac_target = sysconfig.get_config_var('MACOSX_DEPLOYMENT_TARGET')
  if mac_target and (pkg_resources.parse_version(mac_target) <
		     pkg_resources.parse_version('10.9.0')):
    os.environ['MACOSX_DEPLOYMENT_TARGET'] = '10.9'
    os.environ['_PYTHON_HOST_PLATFORM'] = re.sub(
        r'macosx-[0-9]+\.[0-9]+-(.+)',
        r'macosx-10.9-\1',
        util.get_platform())

def package_data():
  tools_path = GRPC_PYTHON_TOOLS_PACKAGE.replace('.', os.path.sep)
  proto_resources_path = os.path.join(tools_path,
                                      GRPC_PYTHON_PROTO_RESOURCES_NAME)
  proto_files = []
  for proto_file in PROTO_FILES:
    source = os.path.join(PROTO_INCLUDE, proto_file)
    target = os.path.join(proto_resources_path, proto_file)
    relative_target = os.path.join(GRPC_PYTHON_PROTO_RESOURCES_NAME, proto_file)
    try:
      os.makedirs(os.path.dirname(target))
    except OSError as error:
      if error.errno == errno.EEXIST:
        pass
      else:
        raise
    shutil.copy(source, target)
    proto_files.append(relative_target)
  return {GRPC_PYTHON_TOOLS_PACKAGE: proto_files}

def protoc_ext_module():
  plugin_sources = [
      os.path.join('grpc', 'tools', 'main.cc'),
      os.path.join('grpc_root', 'src', 'compiler', 'python_generator.cc')] + [
      os.path.join(CC_INCLUDE, cc_file)
      for cc_file in CC_FILES]
  plugin_ext = extension.Extension(
      name='grpc.tools._protoc_compiler',
      sources=(
          [os.path.join('grpc', 'tools', '_protoc_compiler.pyx')] +
          plugin_sources),
      include_dirs=[
          '.',
          'grpc_root',
          os.path.join('grpc_root', 'include'),
          CC_INCLUDE,
      ],
      language='c++',
      define_macros=list(DEFINE_MACROS),
      extra_compile_args=list(EXTRA_COMPILE_ARGS),
      extra_link_args=list(EXTRA_LINK_ARGS),
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
  namespace_packages=['grpc'],
  install_requires=[
    'protobuf>=3.0.0',
    'grpcio>={version}'.format(version=grpc_version.VERSION),
  ],
  package_data=package_data(),
)
