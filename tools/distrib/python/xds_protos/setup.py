#! /usr/bin/env python3
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
"""A PyPI package for xDS protos generated Python code."""

import os

import setuptools
import sys

# Manually insert the source directory into the Python path for local module
# imports to succeed
sys.path.insert(0, os.path.abspath("."))

import grpc_version
import python_version


# Keep this in sync with XDS_PROTOS_GENCODE_GRPC_VERSION
# in tools/buildgen/generate_projects.sh.
XDS_PROTOS_GENCODE_GRPC_VERSION = "1.74.0"

INSTALL_REQUIRES = [
    f"grpcio>={XDS_PROTOS_GENCODE_GRPC_VERSION}",
    "protobuf>=6.31.1,<7.0.0",
]
SETUP_REQUIRES = INSTALL_REQUIRES + [
    f"grpcio-tools>={XDS_PROTOS_GENCODE_GRPC_VERSION}"
]

if __name__ == "__main__":
    setuptools.setup(
        python_requires=f">={python_version.MIN_PYTHON_VERSION}",
        install_requires=INSTALL_REQUIRES,
        setup_requires=SETUP_REQUIRES,
    )
