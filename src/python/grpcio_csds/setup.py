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
CLASSIFIERS = [
    "Development Status :: 5 - Production/Stable",
    "Programming Language :: Python",
    "Programming Language :: Python :: 3",
    "License :: OSI Approved :: Apache Software License",
]

PACKAGE_DIRECTORIES = {
    "": ".",
}

INSTALL_REQUIRES = (
    "protobuf>=6.31.1,<7.0.0",
    # Note: grpcio and xds-protos dependencies are handled by the build process
    # since they are built together as part of the same project
)
SETUP_REQUIRES = INSTALL_REQUIRES

setuptools.setup(
    name="grpcio-csds",
    version=grpc_version.VERSION,
    license="Apache License 2.0",
    description="xDS configuration dump library",
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
)
