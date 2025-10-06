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
"""Setup module for the GRPC Python package's status mapping."""

import os
import sys

import setuptools

# Manually insert the source directory into the Python path for local module
# imports to succeed
sys.path.insert(0, os.path.abspath("."))

# Break import-style to ensure we can actually find our local modules.
import python_version

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


CLASSIFIERS = [
    "Development Status :: 5 - Production/Stable",
    "Programming Language :: Python",
    "Programming Language :: Python :: 3",
] + [
    f"Programming Language :: Python :: {x}"
    for x in python_version.SUPPORTED_PYTHON_VERSIONS
]


INSTALL_REQUIRES = (
    "protobuf>=6.31.1,<7.0.0",
    "grpcio>={version}".format(version=grpc_version.VERSION),
    "googleapis-common-protos>=1.5.5",
)

try:
    import status_commands as _status_commands

    # we are in the build environment, otherwise the above import fails
    COMMAND_CLASS = {
        # Run preprocess from the repository *before* doing any packaging!
        "preprocess": _status_commands.Preprocess,
        "build_package_protos": _NoOpCommand,
    }
except ImportError:
    COMMAND_CLASS = {
        # wire up commands to no-op not to break the external dependencies
        "preprocess": _NoOpCommand,
        "build_package_protos": _NoOpCommand,
    }

if __name__ == "__main__":
    setuptools.setup(
        classifiers=CLASSIFIERS,
        python_requires=f">={python_version.MIN_PYTHON_VERSION}",
        install_requires=INSTALL_REQUIRES,
        cmdclass=COMMAND_CLASS,
    )
