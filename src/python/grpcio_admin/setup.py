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
"""Setup module for admin interface in gRPC Python."""

import os
import sys

import setuptools

# Manually insert the source directory into the Python path for local module
# imports to succeed
sys.path.insert(0, os.path.abspath("."))

import grpc_version
import python_version

INSTALL_REQUIRES = (
    "grpcio-channelz>={version}".format(version=grpc_version.VERSION),
    "grpcio-csds>={version}".format(version=grpc_version.VERSION),
)
SETUP_REQUIRES = INSTALL_REQUIRES

PYTHON_REQUIRES = f">={python_version.MIN_PYTHON_VERSION}"

if __name__ == "__main__":
    setuptools.setup(
        python_requires=f">={python_version.MIN_PYTHON_VERSION}",
        install_requires=INSTALL_REQUIRES,
        setup_requires=SETUP_REQUIRES,
    )
