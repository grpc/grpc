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

"""Patches the spawn() command for windows compilers.

Windows has an 8191 character command line limit, but some compilers
support an @command_file directive where command_file is a file
containing the full command line.
"""

from distutils import ccompiler
import os
import os.path
import shutil
import sys
import tempfile

MAX_COMMAND_LENGTH = 8191

_classic_spawn = ccompiler.CCompiler.spawn

def _commandfile_spawn(self, command):
  command_length = sum([len(arg) for arg in command])
  if os.name == 'nt' and command_length > MAX_COMMAND_LENGTH:
    # Even if this command doesn't support the @command_file, it will
    # fail as is so we try blindly
    print('Command line length exceeded, using command file')
    print(' '.join(command))
    temporary_directory = tempfile.mkdtemp()
    command_filename = os.path.abspath(
    os.path.join(temporary_directory, 'command'))
    with open(command_filename, 'w') as command_file:
      escaped_args = ['"' + arg.replace('\\', '\\\\') + '"' for arg in command[1:]]
      command_file.write(' '.join(escaped_args))
    modified_command = command[:1] + ['@{}'.format(command_filename)]
    try:
      _classic_spawn(self, modified_command)
    finally:
      shutil.rmtree(temporary_directory)
  else:
    _classic_spawn(self, command)


def monkeypatch_spawn():
  """Monkeypatching is dumb, but it's either that or we become maintainers of
     something much, much bigger."""
  ccompiler.CCompiler.spawn = _commandfile_spawn
