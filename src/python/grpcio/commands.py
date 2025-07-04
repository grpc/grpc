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
"""Provides setuptools command classes for the GRPC Python setup process."""

# NOTE(https://github.com/grpc/grpc/issues/24028): allow setuptools to monkey
# patch distutils
import setuptools  # isort:skip

import glob
import os
import os.path
import shutil
import subprocess
import sys
import sysconfig
import traceback
from typing import Any, List

from setuptools.command import build_ext
from setuptools.command import build_py
import support

PYTHON_STEM = os.path.dirname(os.path.abspath(__file__))
GRPC_STEM = os.path.abspath(PYTHON_STEM + "../../../../")
PROTO_STEM = os.path.join(GRPC_STEM, "src", "proto")
PROTO_GEN_STEM = os.path.join(GRPC_STEM, "src", "python", "gens")
CYTHON_STEM = os.path.join(PYTHON_STEM, "grpc", "_cython")


class CommandError(Exception):
    """Simple exception class for GRPC custom commands."""


# TODO(atash): Remove this once PyPI has better Linux bdist support. See
# https://bitbucket.org/pypa/pypi/issues/120/binary-wheels-for-linux-are-not-supported
def _get_grpc_custom_bdist(decorated_basename: str, target_bdist_basename: str) -> str:
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
    from urllib import request

    decorated_path = decorated_basename + GRPC_CUSTOM_BDIST_EXT
    try:
        url = BINARIES_REPOSITORY + "/{target}".format(target=decorated_path)
        bdist_data = request.urlopen(url).read()
    except IOError as error:
        raise CommandError(
            "{}\n\nCould not find the bdist {}: {}".format(
                traceback.format_exc(), decorated_path, error.message
            )
        )
    # Our chosen local bdist path.
    bdist_path = target_bdist_basename + GRPC_CUSTOM_BDIST_EXT
    try:
        with open(bdist_path, "w") as bdist_file:
            bdist_file.write(bdist_data)
    except IOError as error:
        raise CommandError(
            "{}\n\nCould not write grpcio bdist: {}".format(
                traceback.format_exc(), error.message
            )
        )
    return bdist_path


class SphinxDocumentation(setuptools.Command):
    """Command to generate documentation via sphinx."""

    description = "generate sphinx documentation"
    user_options = []

    def initialize_options(self) -> None:
        pass

    def finalize_options(self) -> None:
        pass

    def run(self) -> None:
        # We import here to ensure that setup.py has had a chance to install the
        # relevant package eggs first.
        import sphinx.cmd.build

        source_dir = os.path.join(GRPC_STEM, "doc", "python", "sphinx")
        target_dir = os.path.join(GRPC_STEM, "doc", "build")
        exit_code = sphinx.cmd.build.build_main(
            ["-b", "html", "-W", "--keep-going", source_dir, target_dir]
        )
        if exit_code != 0:
            raise CommandError(
                "Documentation generation has warnings or errors"
            )


class BuildProjectMetadata(setuptools.Command):
    """Command to generate project metadata in a module."""

    description = "build grpcio project metadata files"
    user_options = []

    def initialize_options(self) -> None:
        pass

    def finalize_options(self) -> None:
        pass

    def run(self) -> None:
        with open(
            os.path.join(PYTHON_STEM, "grpc/_grpcio_metadata.py"), "w"
        ) as module_file:
            module_file.write(
                '__version__ = """{}"""'.format(self.distribution.get_version())
            )


class BuildPy(build_py.build_py):
    """Custom project build command."""

    def run(self) -> None:
        self.run_command("build_project_metadata")
        build_py.build_py.run(self)


def _poison_extensions(extensions: List, message: str) -> None:
    """Includes a file that will always fail to compile in all extensions."""
    poison_filename = os.path.join(PYTHON_STEM, "poison.c")
    with open(poison_filename, "w") as poison:
        poison.write("#error {}".format(message))
    for extension in extensions:
        extension.sources = [poison_filename]


def check_and_update_cythonization(extensions: List) -> bool:
    """Replace .pyx files with their generated counterparts and return whether or
    not cythonization still needs to occur."""
    for extension in extensions:
        generated_pyx_sources = []
        other_sources = []
        for source in extension.sources:
            base, file_ext = os.path.splitext(source)
            if file_ext == ".pyx":
                generated_pyx_source = next(
                    (
                        base + gen_ext
                        for gen_ext in (
                            ".c",
                            ".cpp",
                        )
                        if os.path.isfile(base + gen_ext)
                    ),
                    None,
                )
                if generated_pyx_source:
                    generated_pyx_sources.append(generated_pyx_source)
                else:
                    sys.stderr.write("Cython-generated files are missing...\n")
                    return False
            else:
                other_sources.append(source)
        extension.sources = generated_pyx_sources + other_sources
    sys.stderr.write("Found cython-generated files...\n")
    return True


def try_cythonize(extensions: List, linetracing: bool = False, mandatory: bool = True) -> List:
    """Attempt to cythonize the extensions.

    Args:
      extensions: A list of `setuptools.Extension`.
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
                "This package needs to generate C files with Cython but it"
                " cannot. Poisoning extension sources to disallow extension"
                " commands..."
            )
            _poison_extensions(
                extensions,
                (
                    "Extensions have been poisoned due to missing"
                    " Cython-generated code."
                ),
            )
        return extensions
    cython_compiler_directives = {}
    if linetracing:
        additional_define_macros = [("CYTHON_TRACE_NOGIL", "1")]
        cython_compiler_directives["linetrace"] = True
    return Cython.Build.cythonize(
        extensions,
        include_path=[
            include_dir
            for extension in extensions
            for include_dir in extension.include_dirs
        ]
        + [CYTHON_STEM],
        compiler_directives=cython_compiler_directives,
    )


class BuildExt(build_ext.build_ext):
    """Custom build_ext command to enable compiler-specific flags."""

    C_OPTIONS = {
        "unix": ("-pthread",),
        "msvc": (),
    }
    LINK_OPTIONS = {}

    def get_ext_filename(self, ext_name: str) -> str:
        # since python3.5, python extensions' shared libraries use a suffix that corresponds to the value
        # of sysconfig.get_config_var('EXT_SUFFIX') and contains info about the architecture the library targets.
        # E.g. on x64 linux the suffix is ".cpython-XYZ-x86_64-linux-gnu.so"
        # When crosscompiling python wheels, we need to be able to override this suffix
        # so that the resulting file name matches the target architecture and we end up with a well-formed
        # wheel.
        filename = build_ext.build_ext.get_ext_filename(self, ext_name)
        orig_ext_suffix = sysconfig.get_config_var("EXT_SUFFIX")
        new_ext_suffix = os.getenv("GRPC_PYTHON_OVERRIDE_EXT_SUFFIX")
        if new_ext_suffix and filename.endswith(orig_ext_suffix):
            filename = filename[: -len(orig_ext_suffix)] + new_ext_suffix
        return filename

    def build_extensions(self) -> None:
        # This is to let UnixCompiler get either C or C++ compiler options depending on the source.
        # Note that this doesn't work for MSVCCompiler and will be handled by _spawn_patch.py.
        def new_compile(obj: str, src: str, ext: str, cc_args: List[str], extra_postargs: List[str], pp_opts: List[str]) -> None:
            if ext in (".cpp", ".cc"):
                # C++ source
                self.compiler.compile(
                    [obj], extra_postargs=extra_postargs, depends=[src]
                )
            else:
                # C source
                self.compiler.compile(
                    [obj], extra_postargs=extra_postargs, depends=[src]
                )

        self.compiler.compile = new_compile

        # Let's add some compiler-specific options.
        if self.compiler.compiler_type == "unix":
            for extension in self.extensions:
                extension.extra_compile_args += self.C_OPTIONS["unix"]
        elif self.compiler.compiler_type == "msvc":
            for extension in self.extensions:
                extension.extra_compile_args += self.C_OPTIONS["msvc"]

        # Let's add some linker-specific options.
        if self.compiler.compiler_type == "unix":
            for extension in self.extensions:
                extension.extra_link_args += self.LINK_OPTIONS.get("unix", ())
        elif self.compiler.compiler_type == "msvc":
            for extension in self.extensions:
                extension.extra_link_args += self.LINK_OPTIONS.get("msvc", ())

        build_ext.build_ext.build_extensions(self)


class Gather(setuptools.Command):
    """Command to gather project dependencies."""

    description = "gather dependencies for grpcio"
    user_options = [
        ("output", "o", "output file"),
    ]

    def initialize_options(self) -> None:
        self.output = None

    def finalize_options(self) -> None:
        # distutils requires this override.
        pass

    def run(self) -> None:
        pass


class Clean(setuptools.Command):
    """Command to clean build artifacts."""

    description = "Clean build artifacts."
    user_options = [
        ("all", "a", "a phony flag to allow our script to continue"),
    ]

    _FILE_PATTERNS = (
        "pyb",
        "src/python/grpcio/__pycache__/",
        "src/python/grpcio/grpc/_cython/cygrpc.cpp",
        "src/python/grpcio/grpc/_cython/*.so",
        "src/python/grpcio/grpcio.egg-info/",
    )
    _CURRENT_DIRECTORY = os.path.normpath(
        os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "..")
    )

    def initialize_options(self) -> None:
        self.all = None

    def finalize_options(self) -> None:
        pass

    def run(self) -> None:
        for file_pattern in self._FILE_PATTERNS:
            for file_path in glob.glob(
                os.path.join(self._CURRENT_DIRECTORY, file_pattern)
            ):
                if os.path.isdir(file_path):
                    shutil.rmtree(file_path)
                else:
                    os.remove(file_path)
