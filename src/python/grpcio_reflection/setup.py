# Copyright 2016 gRPC authors.
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
"""Setup module for the GRPC Python package's optional reflection."""

import os
import sys

import setuptools

# Ensure we're in the proper directory whether or not we're being used by pip.
os.chdir(os.path.dirname(os.path.abspath(__file__)))

# Break import-style to ensure we can actually find our commands module.
import reflection_commands
import grpc_version

PACKAGE_DIRECTORIES = {
    '': '.',
}

SETUP_REQUIRES = (
    'grpcio-tools>={version}'.format(version=grpc_version.VERSION),)

INSTALL_REQUIRES = ('protobuf>=3.3.0',
                    'grpcio>={version}'.format(version=grpc_version.VERSION),)

COMMAND_CLASS = {
    # Run preprocess from the repository *before* doing any packaging!
    'preprocess': reflection_commands.CopyProtoModules,
    'build_package_protos': reflection_commands.BuildPackageProtos,
}

setuptools.setup(
    name='grpcio-reflection',
    version=grpc_version.VERSION,
    license='Apache License 2.0',
    description='Standard Protobuf Reflection Service for gRPC',
    author='The gRPC Authors',
    author_email='grpc-io@googlegroups.com',
    url='http://www.grpc.io',
    package_dir=PACKAGE_DIRECTORIES,
    packages=setuptools.find_packages('.'),
    install_requires=INSTALL_REQUIRES,
    setup_requires=SETUP_REQUIRES,
    cmdclass=COMMAND_CLASS)
