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

import setuptools

_PACKAGE_PATH = os.path.realpath(os.path.dirname(__file__))
_README_PATH = os.path.join(_PACKAGE_PATH, "README.rst")

# Ensure we're in the proper directory whether or not we're being used by pip.
os.chdir(os.path.dirname(os.path.abspath(__file__)))

import grpc_version

CLASSIFIERS = [
    "Development Status :: 4 - Beta",
    "Programming Language :: Python",
    "Programming Language :: Python :: 3",
    "License :: OSI Approved :: Apache Software License",
]

PACKAGE_DIRECTORIES = {
    "": ".",
}

INSTALL_REQUIRES = (
    "opentelemetry-sdk>=1.25.0",
    "opentelemetry-resourcedetector-gcp>=1.6.0a0",
    "grpcio=={version}".format(version=grpc_version.VERSION),
    "protobuf>=5.26.1,<6.0dev",
)

setuptools.setup(
    name="grpcio-csm-observability",
    version=grpc_version.VERSION,
    description="gRPC Python CSM observability package",
    long_description=open(_README_PATH, "r").read(),
    author="The gRPC Authors",
    author_email="grpc-io@googlegroups.com",
    url="https://grpc.io",
    project_urls={
        "Source Code": "https://github.com/grpc/grpc/tree/master/src/python/grpcio_csm_observability",
        "Bug Tracker": "https://github.com/grpc/grpc/issues",
    },
    license="Apache License 2.0",
    classifiers=CLASSIFIERS,
    package_dir=PACKAGE_DIRECTORIES,
    packages=setuptools.find_packages("."),
    python_requires=">=3.8",
    install_requires=INSTALL_REQUIRES,
)
