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
import grpc_version

import setuptools

WORK_DIR = os.path.dirname(os.path.abspath(__file__))
EXCLUDE_PYTHON_FILES = ["generated_file_import_test.py", "build.py"]

# Use setuptools to build Python package
with open(os.path.join(WORK_DIR, "README.rst"), "r") as f:
    LONG_DESCRIPTION = f.read()
PACKAGES = setuptools.find_packages(where=".", exclude=EXCLUDE_PYTHON_FILES)
CLASSIFIERS = [
    "Development Status :: 3 - Alpha",
    "Programming Language :: Python",
    "Programming Language :: Python :: 3",
    "License :: OSI Approved :: Apache Software License",
]
INSTALL_REQUIRES = [
    "grpcio>=1.49.0",
    "protobuf>=4.21.6,<5.0dev",
]

SETUP_REQUIRES = INSTALL_REQUIRES + ["grpcio-tools>=1.49.0"]

setuptools.setup(
    name="xds-protos",
    version=grpc_version.VERSION,
    packages=PACKAGES,
    description="Generated Python code from envoyproxy/data-plane-api",
    long_description_content_type="text/x-rst",
    long_description=LONG_DESCRIPTION,
    author="The gRPC Authors",
    author_email="grpc-io@googlegroups.com",
    url="https://grpc.io",
    license="Apache License 2.0",
    python_requires=">=3.7",
    install_requires=INSTALL_REQUIRES,
    setup_requires=SETUP_REQUIRES,
    classifiers=CLASSIFIERS,
)
