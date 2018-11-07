# Copyright 2015 gRPC authors.
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
CYTHON_STEM = os.path.join(PYTHON_STEM, 'grpc', '_cython')


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
        raise CommandError('{}\n\nCould not find the bdist {}: {}'.format(
            traceback.format_exc(), decorated_path, error.message))
    # Our chosen local bdist path.
    bdist_path = target_bdist_basename + GRPC_CUSTOM_BDIST_EXT
    try:
        with open(bdist_path, 'w') as bdist_file:
            bdist_file.write(bdist_data)
    except IOError as error:
        raise CommandError('{}\n\nCould not write grpcio bdist: {}'.format(
            traceback.format_exc(), error.message))
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
        import sphinx.cmd.build
        source_dir = os.path.join(GRPC_STEM, 'doc', 'python', 'sphinx')
        target_dir = os.path.join(GRPC_STEM, 'doc', 'build')
        exit_code = sphinx.cmd.build.build_main(
            ['-b', 'html', '-W', '--keep-going', source_dir, target_dir])
        if exit_code is not 0:
            raise CommandError(
                "Documentation generation has warnings or errors")


class BuildProjectMetadata(setuptools.Command):
    """Command to generate project metadata in a module."""

    description = 'build grpcio project metadata files'
    user_options = []

    def initialize_options(self):
        pass

    def finalize_options(self):
        pass

    def run(self):
        with open(os.path.join(PYTHON_STEM, 'grpc/_grpcio_metadata.py'),
                  'w') as module_file:
            module_file.write('__version__ = """{}"""'.format(
                self.distribution.get_version()))


class BuildPy(build_py.build_py):
    """Custom project build command."""

    def run(self):
        self.run_command('build_project_metadata')
        build_py.build_py.run(self)


def _poison_extensions(extensions, message):
    """Includes a file that will always fail to compile in all extensions."""
    poison_filename = os.path.join(PYTHON_STEM, 'poison.c')
    with open(poison_filename, 'w') as poison:
        poison.write('#error {}'.format(message))
    for extension in extensions:
        extension.sources = [poison_filename]


def check_and_update_cythonization(extensions):
    """Replace .pyx files with their generated counterparts and return whether or
     not cythonization still needs to occur."""
    for extension in extensions:
        generated_pyx_sources = []
        other_sources = []
        for source in extension.sources:
            base, file_ext = os.path.splitext(source)
            if file_ext == '.pyx':
                generated_pyx_source = next(
                    (base + gen_ext for gen_ext in (
                        '.c',
                        '.cpp',
                    ) if os.path.isfile(base + gen_ext)), None)
                if generated_pyx_source:
                    generated_pyx_sources.append(generated_pyx_source)
                else:
                    sys.stderr.write('Cython-generated files are missing...\n')
                    return False
            else:
                other_sources.append(source)
        extension.sources = generated_pyx_sources + other_sources
    sys.stderr.write('Found cython-generated files...\n')
    return True


def try_cythonize(extensions, linetracing=False, mandatory=True):
    """Attempt to cythonize the extensions.

  Args:
    extensions: A list of `distutils.extension.Extension`.
    linetracing: A bool indicating whether or not to enable linetracing.
    mandatory: Whether or not having Cython-generated files is mandatory. If it
      is, extensions will be poisoned when they can't be fully generated.
  """
    try:
        # Break import style to ensure we have access to Cython post-setup_requires
        import Cython.Build
    except ImportError:
        if mandatory:
            sys.stderr.write(
                "This package needs to generate C files with Cython but it cannot. "
                "Poisoning extension sources to disallow extension commands...")
            _poison_extensions(
                extensions,
                "Extensions have been poisoned due to missing Cython-generated code."
            )
        return extensions
    cython_compiler_directives = {}
    if linetracing:
        additional_define_macros = [('CYTHON_TRACE_NOGIL', '1')]
        cython_compiler_directives['linetrace'] = True
    return Cython.Build.cythonize(
        extensions,
        include_path=[
            include_dir
            for extension in extensions
            for include_dir in extension.include_dirs
        ] + [CYTHON_STEM],
        compiler_directives=cython_compiler_directives)


class BuildExt(build_ext.build_ext):
    """Custom build_ext command to enable compiler-specific flags."""

    C_OPTIONS = {
        'unix': ('-pthread',),
        'msvc': (),
    }
    LINK_OPTIONS = {}

    def build_extensions(self):
        # This special conditioning is here due to difference of compiler
        #   behavior in gcc and clang. The clang doesn't take --stdc++11
        #   flags but gcc does. Since the setuptools of Python only support
        #   all C or all C++ compilation, the mix of C and C++ will crash.
        #   *By default*, the macOS use clang and Linux use gcc, that's why
        #   the special condition here is checking platform.
        if "darwin" in sys.platform:
            config = os.environ.get('CONFIG', 'opt')
            target_path = os.path.abspath(
                os.path.join(
                    os.path.dirname(os.path.realpath(__file__)), '..', '..',
                    '..', 'libs', config))
            targets = [
                os.path.join(target_path, 'libboringssl.a'),
                os.path.join(target_path, 'libares.a'),
                os.path.join(target_path, 'libgpr.a'),
                os.path.join(target_path, 'libgrpc.a')
            ]
            # Running make separately for Mac means we lose all
            # Extension.define_macros configured in setup.py. Re-add the macro
            # for gRPC Core's fork handlers.
            # TODO(ericgribkoff) Decide what to do about the other missing core
            #   macros, including GRPC_ENABLE_FORK_SUPPORT, which defaults to 1
            #   on Linux but remains unset on Mac.
            extra_defines = [
                'EXTRA_DEFINES="GRPC_POSIX_FORK_ALLOW_PTHREAD_ATFORK=1"'
            ]
            # Ensure the BoringSSL are built instead of using system provided
            #   libraries. It prevents dependency issues while distributing to
            #   Mac users who use MacPorts to manage their libraries. #17002
            mod_env = dict(os.environ)
            mod_env['REQUIRE_CUSTOM_LIBRARIES_opt'] = '1'
            make_process = subprocess.Popen(
                ['make'] + extra_defines + targets,
                env=mod_env,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE)
            make_out, make_err = make_process.communicate()
            if make_out and make_process.returncode != 0:
                sys.stdout.write(str(make_out) + '\n')
            if make_err:
                sys.stderr.write(str(make_err) + '\n')
            if make_process.returncode != 0:
                raise Exception("make command failed!")

        compiler = self.compiler.compiler_type
        if compiler in BuildExt.C_OPTIONS:
            for extension in self.extensions:
                extension.extra_compile_args += list(
                    BuildExt.C_OPTIONS[compiler])
        if compiler in BuildExt.LINK_OPTIONS:
            for extension in self.extensions:
                extension.extra_link_args += list(
                    BuildExt.LINK_OPTIONS[compiler])
        if not check_and_update_cythonization(self.extensions):
            self.extensions = try_cythonize(self.extensions)
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
    user_options = [('test', 't',
                     'flag indicating to gather test dependencies'),
                    ('install', 'i',
                     'flag indicating to gather install dependencies')]

    def initialize_options(self):
        self.test = False
        self.install = False

    def finalize_options(self):
        # distutils requires this override.
        pass

    def run(self):
        if self.install and self.distribution.install_requires:
            self.distribution.fetch_build_eggs(
                self.distribution.install_requires)
        if self.test and self.distribution.tests_require:
            self.distribution.fetch_build_eggs(self.distribution.tests_require)
