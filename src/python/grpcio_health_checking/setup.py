# Copyright 2015, Google Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
"""Setup module for the GRPC Python package's optional health checking."""

import os
import sys

import setuptools

# Ensure we're in the proper directory whether or not we're being used by pip.
os.chdir(os.path.dirname(os.path.abspath(__file__)))

# Break import-style to ensure we can actually find our commands module.
import health_commands
import grpc_version

PACKAGE_DIRECTORIES = {
    '': '.',
}

SETUP_REQUIRES = (
    'grpcio-tools>={version}'.format(version=grpc_version.VERSION),)

INSTALL_REQUIRES = ('protobuf>=3.2.0',
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
    url='http://www.grpc.io',
    license='3-clause BSD',
    package_dir=PACKAGE_DIRECTORIES,
    packages=setuptools.find_packages('.'),
    install_requires=INSTALL_REQUIRES,
    setup_requires=SETUP_REQUIRES,
    cmdclass=COMMAND_CLASS)
