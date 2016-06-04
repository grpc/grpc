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

"""Covers inadequacies in distutils."""

from distutils import ccompiler
from distutils import errors
from distutils import unixccompiler
import os
import os.path
import shutil
import sys
import tempfile


def _unix_piecemeal_link(
    self, target_desc, objects, output_filename, output_dir=None,
    libraries=None, library_dirs=None, runtime_library_dirs=None,
    export_symbols=None, debug=0, extra_preargs=None, extra_postargs=None,
    build_temp=None, target_lang=None):
  """`link` externalized method taken almost verbatim from UnixCCompiler.

  Modifies the link command for unix-like compilers by using a command file so
  that long command line argument strings don't break the command shell's
  ARG_MAX character limit.
  """
  objects, output_dir = self._fix_object_args(objects, output_dir)
  libraries, library_dirs, runtime_library_dirs = self._fix_lib_args(
      libraries, library_dirs, runtime_library_dirs)
  # filter out standard library paths, which are not explicitely needed
  # for linking
  library_dirs = [dir for dir in library_dirs
                  if not dir in ('/lib', '/lib64', '/usr/lib', '/usr/lib64')]
  runtime_library_dirs = [dir for dir in runtime_library_dirs
                          if not dir in ('/lib', '/lib64', '/usr/lib', '/usr/lib64')]
  lib_opts = ccompiler.gen_lib_options(self, library_dirs, runtime_library_dirs,
                             libraries)
  if not isinstance(output_dir, basestring) and output_dir is not None:
    raise TypeError, "'output_dir' must be a string or None"
  if output_dir is not None:
    output_filename = os.path.join(output_dir, output_filename)

  if self._need_link(objects, output_filename):
    ld_args = (objects + self.objects +
               lib_opts + ['-o', output_filename])
    if debug:
      ld_args[:0] = ['-g']
    if extra_preargs:
      ld_args[:0] = extra_preargs
    if extra_postargs:
      ld_args.extend(extra_postargs)
    self.mkpath(os.path.dirname(output_filename))
    try:
      if target_desc == ccompiler.CCompiler.EXECUTABLE:
        linker = self.linker_exe[:]
      else:
        linker = self.linker_so[:]
      if target_lang == "c++" and self.compiler_cxx:
        # skip over environment variable settings if /usr/bin/env
        # is used to set up the linker's environment.
        # This is needed on OSX. Note: this assumes that the
        # normal and C++ compiler have the same environment
        # settings.
        i = 0
        if os.path.basename(linker[0]) == "env":
          i = 1
          while '=' in linker[i]:
            i = i + 1

        linker[i] = self.compiler_cxx[i]

      if sys.platform == 'darwin':
        import _osx_support
        linker = _osx_support.compiler_fixup(linker, ld_args)

      temporary_directory = tempfile.mkdtemp()
      command_filename = os.path.abspath(
          os.path.join(temporary_directory, 'command'))
      with open(command_filename, 'w') as command_file:
        escaped_ld_args = [arg.replace('\\', '\\\\') for arg in ld_args]
        command_file.write(' '.join(escaped_ld_args))
      self.spawn(linker + ['@{}'.format(command_filename)])
    except errors.DistutilsExecError, msg:
      raise ccompiler.LinkError, msg
  else:
    log.debug("skipping %s (up-to-date)", output_filename)

def monkeypatch_unix_compiler():
  """Monkeypatching is dumb, but it's either that or we become maintainers of
     something much, much bigger."""
  unixccompiler.UnixCCompiler.link = _unix_piecemeal_link
