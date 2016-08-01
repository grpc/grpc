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
import shlex
import shutil
import sys
import tempfile

def _unix_commandfile_spawn(self, command):
  """Wrapper around distutils.util.spawn that attempts to use command files.

  Meant to replace the CCompiler method `spawn` on UnixCCompiler and its
  derivatives (e.g. the MinGW32 compiler).

  Some commands like `gcc` (and friends like `clang`) support command files to
  work around shell command length limits.
  """
  # Sometimes distutils embeds the executables as full strings including some
  # hard-coded flags rather than as lists.
  command = list(shlex.split(command[0])) + list(command[1:])
  command_base = os.path.basename(command[0].strip())
  if command_base == 'ccache':
    command_base = command[:2]
    command_args = command[2:]
  elif command_base.startswith('ccache') or command_base in ['gcc', 'clang', 'clang++', 'g++']:
    command_base = command[:1]
    command_args = command[1:]
  else:
    return ccompiler.CCompiler.spawn(self, command)
  temporary_directory = tempfile.mkdtemp()
  command_filename = os.path.abspath(os.path.join(temporary_directory, 'command'))
  with open(command_filename, 'w') as command_file:
    escaped_args = [arg.replace('\\', '\\\\') for arg in command_args]
    command_file.write(' '.join(escaped_args))
  modified_command = command_base + ['@{}'.format(command_filename)]
  result = ccompiler.CCompiler.spawn(self, modified_command)
  shutil.rmtree(temporary_directory)
  return result


def monkeypatch_unix_compiler():
  """Monkeypatching is dumb, but it's either that or we become maintainers of
     something much, much bigger."""
  unixccompiler.UnixCCompiler.spawn = _unix_commandfile_spawn
