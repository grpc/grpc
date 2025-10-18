# Copyright 2024 The gRPC Authors
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

import os
import sys

import setuptools

# Manually insert the source directory into the Python path for local module
# imports to succeed
sys.path.insert(0, os.path.abspath("."))

import grpc_version
import python_version

INSTALL_REQUIRES = (
    "opentelemetry-sdk>=1.25.0",
    "opentelemetry-resourcedetector-gcp>=1.6.0a0",
    "grpcio=={version}".format(version=grpc_version.VERSION),
    "protobuf>=6.31.1,<7.0.0",
)

if __name__ == "__main__":
    setuptools.setup(
        python_requires=f">={python_version.MIN_PYTHON_VERSION}",
        install_requires=INSTALL_REQUIRES,
    )
