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
"""Patches the spawn() command for windows compilers.

Windows has an 8191 character command line limit, but some compilers
support an @command_file directive where command_file is a file
containing the full command line.
"""

import distutils
from distutils import ccompiler
import os
import os.path
import shutil
import sys
import tempfile

MAX_COMMAND_LENGTH = 8191

_classic_spawn = ccompiler.CCompiler.spawn

def get_setuptools_temp_dir():
    # Get the Platform/Architecture Part ('win32' / 'win-amd64')
    platform_tag = distutils.util.get_platform()

    # Get the Python Implementation and Version Part (e.g., 'cpython-314')

    # Get the implementation name (usually 'cpython')
    impl_name = sys.implementation.name

    # Get major and minor version numbers
    python_major = sys.version_info.major
    python_minor = sys.version_info.minor

    # Combine them into the format used by setuptools
    python_tag = f"{impl_name}-{python_major}{python_minor}"

    # Combine them to a format like `temp.win32-cpython-314`
    full_setuptools_dir = f"temp.{platform_tag}-{python_tag}"

    return full_setuptools_dir


def _commandfile_spawn(self, command, **kwargs):
    if os.name == "nt":
        if any(arg.startswith("/Tc") for arg in command):
            # Remove /std:c++17 option if this is a MSVC C complation
            command = [arg for arg in command if arg != "/std:c++17"]
        elif any(arg.startswith("/Tp") for arg in command):
            # Remove /std:c11 option if this is a MSVC C++ complation
            command = [arg for arg in command if arg != "/std:c11"]

    command_length = sum([len(arg) for arg in command])
    if os.name == "nt" and command_length > MAX_COMMAND_LENGTH:
        # Even if this command doesn't support the @command_file, it will
        # fail as is so we try blindly

        build_temp = os.path.join("pyb", get_setuptools_temp_dir())
        #os.environ.get("BUILD_EXT_TEMP")

        print("Using temp directory:", build_temp)
        release_build_dir = os.path.join(build_temp, "Release")
        third_party_dir = os.path.join(release_build_dir, "third_party")

        print("release_build_dir:", release_build_dir)
        print("third_party_dir:", third_party_dir)

        os.environ["BUILD_DIR"] = release_build_dir
        os.environ["THIRD_PARTY"] = third_party_dir

        def replace_and_escape_paths(path: str):
            old_path = path
            path.replace(third_party_dir, "%THIRD_PARTY%")
            path.replace(release_build_dir, "%BUILD_DIR%")
            path = '"' + path.replace("\\", "\\\\") + '"'
            print("Converted", old_path, "->", path)
            return path

        print("Command line length exceeded, using command file")
        print(" ".join(command))
        temporary_directory = tempfile.mkdtemp()
        command_filename = os.path.abspath(
            os.path.join(temporary_directory, "command")
        )
        with open(command_filename, "w") as command_file:
            escaped_args = [
                replace_and_escape_paths(arg) for arg in command[1:]
            ]
            # add each arg on a separate line to avoid hitting the
            # "line in command file contains 131071 or more characters" error
            # (can happen for extra long link commands)
            command_file.write(" \n".join(escaped_args))
        modified_command = command[:1] + ["@{}".format(command_filename)]
        try:
            _classic_spawn(self, modified_command, **kwargs)
        finally:
            shutil.rmtree(temporary_directory)
    else:
        _classic_spawn(self, command, **kwargs)


def monkeypatch_spawn():
    """Monkeypatching is dumb, but it's either that or we become maintainers of
    something much, much bigger."""
    ccompiler.CCompiler.spawn = _commandfile_spawn
