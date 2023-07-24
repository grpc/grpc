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
import grpc_version

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
    "protobuf>=4.21.6",
    f"xds-protos=={grpc_version.VERSION}",
    f"grpcio>={grpc_version.VERSION}",
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
    python_requires=">=3.6",
    install_requires=INSTALL_REQUIRES,
    setup_requires=SETUP_REQUIRES,
)
