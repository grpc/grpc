# Copyright 2017 gRPC authors.
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
"""Setup module for gRPC Python's testing package."""

import os
import sys

import setuptools

_PACKAGE_PATH = os.path.realpath(os.path.dirname(__file__))
_README_PATH = os.path.join(_PACKAGE_PATH, "README.rst")

# Ensure we're in the proper directory whether or not we're being used by pip.
os.chdir(os.path.dirname(os.path.abspath(__file__)))

# Break import style to ensure that we can find same-directory modules.
import grpc_version


class _NoOpCommand(setuptools.Command):
    """No-op command."""

    description = ""
    user_options = []

    def initialize_options(self):
        pass

    def finalize_options(self):
        pass

    def run(self):
        pass


PACKAGE_DIRECTORIES = {
    "": ".",
}

INSTALL_REQUIRES = (
    "protobuf>=5.26.1,<6.0dev",
    "grpcio>={version}".format(version=grpc_version.VERSION),
)

try:
    import testing_commands as _testing_commands

    # we are in the build environment, otherwise the above import fails
    COMMAND_CLASS = {
        # Run preprocess from the repository *before* doing any packaging!
        "preprocess": _testing_commands.Preprocess,
    }
except ImportError:
    COMMAND_CLASS = {
        # wire up commands to no-op not to break the external dependencies
        "preprocess": _NoOpCommand,
    }

setuptools.setup(
    name="grpcio-testing",
    version=grpc_version.VERSION,
    license="Apache License 2.0",
    description="Testing utilities for gRPC Python",
    long_description=open(_README_PATH, "r").read(),
    author="The gRPC Authors",
    author_email="grpc-io@googlegroups.com",
    url="https://grpc.io",
    package_dir=PACKAGE_DIRECTORIES,
    packages=setuptools.find_packages("."),
    install_requires=INSTALL_REQUIRES,
    cmdclass=COMMAND_CLASS,
)
