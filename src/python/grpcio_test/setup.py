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

"""A setup module for the GRPC Python interop testing package."""

import os
import os.path

import setuptools

# Ensure we're in the proper directory whether or not we're being used by pip.
os.chdir(os.path.dirname(os.path.abspath(__file__)))

# Break import-style to ensure we can actually find our commands module.
import commands

_PACKAGES = setuptools.find_packages('.')

_PACKAGE_DIRECTORIES = {
    '': '.',
}

_PACKAGE_DATA = {
    'grpc_interop': [
        'credentials/ca.pem',
        'credentials/server1.key',
        'credentials/server1.pem',
    ],
    'grpc_protoc_plugin': [
        'test.proto',
    ],
    'grpc_test': [
        'credentials/ca.pem',
        'credentials/server1.key',
        'credentials/server1.pem',
    ],
}

_SETUP_REQUIRES = (
    'pytest>=2.6',
    'pytest-cov>=2.0',
    'pytest-xdist>=1.11',
    'pytest-timeout>=0.5',
)

_INSTALL_REQUIRES = (
    'oauth2client>=1.4.7',
    'grpcio>=0.11.0b0',
    # TODO(issue 3321): Unpin protobuf dependency.
    'protobuf==3.0.0a3',
)

_COMMAND_CLASS = {
    'test': commands.RunTests,
    'build_proto_modules': commands.BuildProtoModules,
    'build_py': commands.BuildPy,
}

setuptools.setup(
    name='grpcio_test',
    version='0.11.0b0',
    packages=_PACKAGES,
    package_dir=_PACKAGE_DIRECTORIES,
    package_data=_PACKAGE_DATA,
    install_requires=_INSTALL_REQUIRES + _SETUP_REQUIRES,
    setup_requires=_SETUP_REQUIRES,
    cmdclass=_COMMAND_CLASS,
)
