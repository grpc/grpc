# Copyright 2018 The gRPC Authors
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
"""Setup module for the GRPC Python package's Channelz."""

import os
import sys

import setuptools

_PACKAGE_PATH = os.path.realpath(os.path.dirname(__file__))
_README_PATH = os.path.join(_PACKAGE_PATH, "README.rst")

# Ensure we're in the proper directory whether or not we're being used by pip.
os.chdir(os.path.dirname(os.path.abspath(__file__)))

# Break import-style to ensure we can actually find our local modules.
try:
    import python_version
    # Check if it has the required attributes (local module vs PyPI package)
    if not hasattr(python_version, 'MIN_PYTHON_VERSION'):
        raise ImportError("python_version missing required attributes")
except ImportError:
    # Fallback when python_version is not available or doesn't have required attributes
    class python_version:
        MIN_PYTHON_VERSION = 3.9
        SUPPORTED_PYTHON_VERSIONS = ["3.9", "3.10", "3.11", "3.12", "3.13", "3.14"]
        MAX_PYTHON_VERSION = 3.14
try:
    import grpc_version
except ImportError:
    # Fallback when grpc_version is not available in build environment
    class grpc_version:
        VERSION = "1.76.0.dev0"

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


CLASSIFIERS = (
    [
        "Development Status :: 5 - Production/Stable",
        "Programming Language :: Python",
        "Programming Language :: Python :: 3",
    ]
    + [
        f"Programming Language :: Python :: {x}"
        for x in python_version.SUPPORTED_PYTHON_VERSIONS
    ]
    + ["License :: OSI Approved :: Apache Software License"]
)


PACKAGE_DIRECTORIES = {
    "": ".",
}

INSTALL_REQUIRES = (
    "protobuf>=6.31.1,<7.0.0",
    "grpcio>={version}".format(version=grpc_version.VERSION),
)

try:
    import channelz_commands as _channelz_commands

    # we are in the build environment, otherwise the above import fails
    SETUP_REQUIRES = (
        "grpcio-tools=={version}".format(version=grpc_version.VERSION),
    )
    COMMAND_CLASS = {
        # Run preprocess from the repository *before* doing any packaging!
        "preprocess": _channelz_commands.Preprocess,
        "build_package_protos": _channelz_commands.BuildPackageProtos,
    }
except ImportError:
    SETUP_REQUIRES = ()
    COMMAND_CLASS = {
        # wire up commands to no-op not to break the external dependencies
        "preprocess": _NoOpCommand,
        "build_package_protos": _NoOpCommand,
    }


setuptools.setup(
    name="grpcio-channelz",
    version=grpc_version.VERSION,
    license="Apache License 2.0",
    description="Channel Level Live Debug Information Service for gRPC",
    long_description=open(_README_PATH, "r").read(),
    author="The gRPC Authors",
    author_email="grpc-io@googlegroups.com",
    classifiers=CLASSIFIERS,
    url="https://grpc.io",
    package_dir=PACKAGE_DIRECTORIES,
    packages=setuptools.find_packages("."),
    python_requires=f">={python_version.MIN_PYTHON_VERSION}",
    install_requires=INSTALL_REQUIRES,
    setup_requires=SETUP_REQUIRES,
    cmdclass=COMMAND_CLASS,
)
