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

from distutils import ccompiler
import os
import os.path
import shutil
import sys
import tempfile

MAX_COMMAND_LENGTH = 8191

_classic_spawn = ccompiler.CCompiler.spawn


def escape_arg(arg):
    # Escape single backslash with double backslash
    arg = arg.replace("\\", "\\\\")

    # Escape double quotes with a backslash
    arg = arg.replace('"', '\\"')

    return f'"{arg}"'


def _commandfile_spawn(self, command, **kwargs):
    if os.name == "nt":
        if any(arg.startswith("/Tc") for arg in command):
            # Remove /std:c++17 option if this is a MSVC C complation
            command = [arg for arg in command if arg != "/std:c++17"]
        elif any(arg.startswith("/Tp") for arg in command):
            # Remove /std:c11 option if this is a MSVC C++ complation
            command = [arg for arg in command if arg != "/std:c11"]

    enable_ccache = os.environ.get("GRPC_BUILD_ENABLE_CCACHE", "").lower() in [
        "true",
        "1",
    ]
    has_ccache = shutil.which("ccache") is not None
    use_ccache = enable_ccache and has_ccache and command[0].endswith("cl.exe")

    if use_ccache:
        # Workaround for ccache 4.8 not handling certain flags properly:

        new_command = []
        for arg in command:
            # /Tp, /Tc:
            #    ccache performs a preprocessor run to generate cachable
            #    file, and /Tp /Tc flags seems to be repositioned such that
            #    the msvc backend doesn't recognize it.
            #    e.g. /SomeFlag /Tpfile.cc -> /Tp /SomeFlag file.cc
            if arg.startswith("/Tp") and len(arg) > 3:
                # Replace with the global variant. This is fine for
                # setuptools build because it only builds one file at
                # a time.
                new_command.extend(["/TP", arg[3:]])
            elif arg.startswith("/Tc") and len(arg) > 3:
                new_command.extend(["/TC", arg[3:]])
            #   /Zc:preprocessor:
            #      This is not supported by ccache but needed for protobuf 33.5
            #      See also https://github.com/grpc/grpc/issues/41951
            elif arg == "/Zc:preprocessor":
                new_command.extend(["--ccache-skip", "/Zc:preprocessor"])
            else:
                new_command.append(arg)
        command = new_command

    command_length = sum([len(arg) for arg in command])
    if os.name == "nt" and command_length > MAX_COMMAND_LENGTH:
        # Even if this command doesn't support the @command_file, it will
        # fail as is so we try blindly
        print("Command line length exceeded, using command file")
        print(" ".join(command))
        temporary_directory = tempfile.mkdtemp()
        command_filename = os.path.abspath(
            os.path.join(temporary_directory, "command")
        )
        with open(command_filename, "w") as command_file:
            escaped_args = map(escape_arg, command[1:])
            # add each arg on a separate line to avoid hitting the
            # "line in command file contains 131071 or more characters" error
            # (can happen for extra long link commands)
            command_file.write(" \n".join(escaped_args))

        if use_ccache:
            modified_command = (
                ["ccache"] + command[:1] + ["@{}".format(command_filename)]
            )
        else:
            modified_command = command[:1] + ["@{}".format(command_filename)]

        try:
            _classic_spawn(self, modified_command, **kwargs)
        finally:
            shutil.rmtree(temporary_directory)
    else:
        if use_ccache:
            _classic_spawn(self, ["ccache"] + command, **kwargs)
        else:
            _classic_spawn(self, command, **kwargs)


def monkeypatch_spawn():
    """Monkeypatching is dumb, but it's either that or we become maintainers of
    something much, much bigger."""
    ccompiler.CCompiler.spawn = _commandfile_spawn
