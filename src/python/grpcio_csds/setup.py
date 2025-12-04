# Copyright 2021 The gRPC Authors
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
"""Setup module for CSDS in gRPC Python."""

import os
import sys

import setuptools

# Manually insert the source directory into the Python path for local module
# imports to succeed
sys.path.insert(0, os.path.abspath("."))

import grpc_version
import python_version

INSTALL_REQUIRES = (
    "protobuf>=6.31.1,<7.0.0",
    f"xds-protos=={grpc_version.VERSION}",
    f"grpcio>={grpc_version.VERSION}",
)
SETUP_REQUIRES = INSTALL_REQUIRES

PYTHON_REQUIRES = f">={python_version.MIN_PYTHON_VERSION}"

CLASSIFIERS = [
    "Development Status :: 5 - Production/Stable",
    "Programming Language :: Python",
    "Programming Language :: Python :: 3",
] + [
    f"Programming Language :: Python :: {x}"
    for x in python_version.SUPPORTED_PYTHON_VERSIONS
]

if __name__ == "__main__":
    setuptools.setup(
        classifiers=CLASSIFIERS,
        python_requires=f">={python_version.MIN_PYTHON_VERSION}",
        install_requires=INSTALL_REQUIRES,
        setup_requires=SETUP_REQUIRES,
    )
