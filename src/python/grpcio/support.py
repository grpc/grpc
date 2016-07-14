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


import os
import os.path
import shutil
import sys
import tempfile

from distutils import errors

import commands


C_PYTHON_DEV = """
#include <Python.h>
int main(int argc, char **argv) { return 0; }
"""
C_PYTHON_DEV_ERROR_MESSAGE = """
Could not find <Python.h>. This could mean the following:
  * You're on Ubuntu and haven't `apt-get install`ed `python-dev`.
  * You're on Mac OS X and the usual Python framework was somehow corrupted
    (check your environment variables or try re-installing?)
  * You're on Windows and your Python installation was somehow corrupted
    (check your environment variables or try re-installing?)
"""

C_CHECKS = {
  C_PYTHON_DEV: C_PYTHON_DEV_ERROR_MESSAGE,
}

def _compile(compiler, source_string):
  tempdir = tempfile.mkdtemp()
  cpath = os.path.join(tempdir, 'a.c')
  with open(cpath, 'w') as cfile:
    cfile.write(source_string)
  try:
    compiler.compile([cpath])
  except errors.CompileError as error:
    return error
  finally:
    shutil.rmtree(tempdir)

def _expect_compile(compiler, source_string, error_message):
  if _compile(compiler, source_string) is not None:
    sys.stderr.write(error_message)
    raise commands.CommandError(
        "Diagnostics found a compilation environment issue:\n{}"
            .format(error_message))

def diagnose_compile_error(build_ext, error):
  """Attempt to diagnose an error during compilation."""
  for c_check, message in C_CHECKS.items():
    _expect_compile(build_ext.compiler, c_check, message)
  python_sources = [
      source for source in build_ext.get_source_files()
      if source.startswith('./src/python') and source.endswith('c')
  ]
  for source in python_sources:
    if not os.path.isfile(source):
      raise commands.CommandError(
          ("Diagnostics found a missing Python extension source file:\n{}\n\n"
           "This is usually because the Cython sources haven't been transpiled "
           "into C yet and you're building from source.\n"
           "Try setting the environment variable "
           "`GRPC_PYTHON_BUILD_WITH_CYTHON=1` when invoking `setup.py` or "
           "when using `pip`, e.g.:\n\n"
           "pip install -rrequirements.txt\n"
           "GRPC_PYTHON_BUILD_WITH_CYTHON=1 pip install .")
            .format(source)
          )


_ERROR_DIAGNOSES = {
    errors.CompileError: diagnose_compile_error
}

def diagnose_build_ext_error(build_ext, error, formatted):
  diagnostic = _ERROR_DIAGNOSES.get(type(error))
  if diagnostic is None:
    raise commands.CommandError(
        "\n\nWe could not diagnose your build failure. Please file an issue at "
        "http://www.github.com/grpc/grpc with `[Python install]` in the title."
        "\n\n{}".format(formatted))
  else:
    diagnostic(build_ext, error)

