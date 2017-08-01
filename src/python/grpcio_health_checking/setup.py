# Copyright 2015 gRPC authors.
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
"""Setup module for the GRPC Python package's optional health checking."""

import os

import setuptools

# Ensure we're in the proper directory whether or not we're being used by pip.
os.chdir(os.path.dirname(os.path.abspath(__file__)))

# Break import-style to ensure we can actually find our commands module.
import health_commands
import grpc_version

CLASSIFIERS = [
    'Development Status :: 5 - Production/Stable',
    'Programming Language :: Python',
    'Programming Language :: Python :: 2',
    'Programming Language :: Python :: 2.7',
    'Programming Language :: Python :: 3',
    'Programming Language :: Python :: 3.4',
    'Programming Language :: Python :: 3.5',
    'Programming Language :: Python :: 3.6',
    'License :: OSI Approved :: Apache Software License',
],

PACKAGE_DIRECTORIES = {
    '': '.',
}

SETUP_REQUIRES = (
    'grpcio-tools>={version}'.format(version=grpc_version.VERSION),)

INSTALL_REQUIRES = ('protobuf>=3.3.0',
                    'grpcio>={version}'.format(version=grpc_version.VERSION),)

COMMAND_CLASS = {
    # Run preprocess from the repository *before* doing any packaging!
    'preprocess': health_commands.CopyProtoModules,
    'build_package_protos': health_commands.BuildPackageProtos,
}

setuptools.setup(
    name='grpcio-health-checking',
    version=grpc_version.VERSION,
    description='Standard Health Checking Service for gRPC',
    author='The gRPC Authors',
    author_email='grpc-io@googlegroups.com',
    url='https://grpc.io',
    license='Apache License 2.0',
    classifiers=CLASSIFIERS,
    package_dir=PACKAGE_DIRECTORIES,
    packages=setuptools.find_packages('.'),
    install_requires=INSTALL_REQUIRES,
    setup_requires=SETUP_REQUIRES,
    cmdclass=COMMAND_CLASS)
