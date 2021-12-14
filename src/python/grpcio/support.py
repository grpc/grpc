# Copyright 2016 gRPC authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

from distutils import errors
import os
import os.path
import shutil
import sys
import tempfile

import commands

C_PYTHON_DEV = """
#include <Python.h>
int main(int argc, char **argv) { return 0; }
"""
C_PYTHON_DEV_ERROR_MESSAGE = """
Could not find <Python.h>. This could mean the following:
  * You're on Ubuntu and haven't run `apt-get install <PY_REPR>-dev`.
  * You're on RHEL/Fedora and haven't run `yum install <PY_REPR>-devel` or
    `dnf install <PY_REPR>-devel` (make sure you also have redhat-rpm-config
    installed)
  * You're on Mac OS X and the usual Python framework was somehow corrupted
    (check your environment variables or try re-installing?)
  * You're on Windows and your Python installation was somehow corrupted
    (check your environment variables or try re-installing?)
"""
if sys.version_info[0] == 2:
    PYTHON_REPRESENTATION = 'python'
elif sys.version_info[0] == 3:
    PYTHON_REPRESENTATION = 'python3'
else:
    raise NotImplementedError('Unsupported Python version: %s' % sys.version)

C_CHECKS = {
    C_PYTHON_DEV:
        C_PYTHON_DEV_ERROR_MESSAGE.replace('<PY_REPR>', PYTHON_REPRESENTATION),
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
            "Diagnostics found a compilation environment issue:\n{}".format(
                error_message))


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
            raise commands.CommandError((
                "Diagnostics found a missing Python extension source file:\n{}\n\n"
                "This is usually because the Cython sources haven't been transpiled "
                "into C yet and you're building from source.\n"
                "Try setting the environment variable "
                "`GRPC_PYTHON_BUILD_WITH_CYTHON=1` when invoking `setup.py` or "
                "when using `pip`, e.g.:\n\n"
                "pip install -rrequirements.txt\n"
                "GRPC_PYTHON_BUILD_WITH_CYTHON=1 pip install .").format(source))


def diagnose_attribute_error(build_ext, error):
    if any('_needs_stub' in arg for arg in error.args):
        raise commands.CommandError(
            "We expect a missing `_needs_stub` attribute from older versions of "
            "setuptools. Consider upgrading setuptools.")


_ERROR_DIAGNOSES = {
    errors.CompileError: diagnose_compile_error,
    AttributeError: diagnose_attribute_error,
}


def diagnose_build_ext_error(build_ext, error, formatted):
    diagnostic = _ERROR_DIAGNOSES.get(type(error))
    if diagnostic is None:
        raise commands.CommandError(
            "\n\nWe could not diagnose your build failure. If you are unable to "
            "proceed, please file an issue at http://www.github.com/grpc/grpc "
            "with `[Python install]` in the title; please attach the whole log "
            "(including everything that may have appeared above the Python "
            "backtrace).\n\n{}".format(formatted))
    else:
        diagnostic(build_ext, error)
