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

"""A setup module for the gRPC Python package."""

import os
import os.path
import shutil
import sys

from distutils import core as _core
from distutils import extension as _extension
import setuptools
from setuptools.command import egg_info

import grpc.tools.command

PY3 = sys.version_info.major == 3

# Ensure we're in the proper directory whether or not we're being used by pip.
os.chdir(os.path.dirname(os.path.abspath(__file__)))

# Break import-style to ensure we can actually find our in-repo dependencies.
import commands
import grpc_version

LICENSE = '3-clause BSD'

PACKAGE_DIRECTORIES = {
    '': '.',
}

INSTALL_REQUIRES = (
    'coverage>=4.0',
    'enum34>=1.0.4',
    'futures>=2.2.0',
    'grpcio>=0.14.0',
    'grpcio-health-checking>=0.14.0',
    'oauth2client>=1.4.7',
    'protobuf>=3.0.0a3',
    'six>=1.10',
)

SETUP_REQUIRES = (
    'grpcio-tools>=0.14.0',
)

COMMAND_CLASS = {
    # Run `preprocess` *before* doing any packaging!
    'preprocess': commands.GatherProto,

    'build_proto_modules': grpc.tools.command.BuildProtoModules,
    'build_py': commands.BuildPy,
    'run_interop': commands.RunInterop,
    'test_lite': commands.TestLite
}

PACKAGE_DATA = {
    'tests.interop': [
        'credentials/ca.pem',
        'credentials/server1.key',
        'credentials/server1.pem',
    ],
    'tests.protoc_plugin': [
        'protoc_plugin_test.proto',
    ],
    'tests.unit': [
        'credentials/ca.pem',
        'credentials/server1.key',
        'credentials/server1.pem',
    ],
    'tests': [
        'tests.json'
    ],
}

TEST_SUITE = 'tests'
TEST_LOADER = 'tests:Loader'
TEST_RUNNER = 'tests:Runner'
TESTS_REQUIRE = INSTALL_REQUIRES

PACKAGES = setuptools.find_packages('.')

setuptools.setup(
  name='grpcio-tests',
  version=grpc_version.VERSION,
  license=LICENSE,
  packages=list(PACKAGES),
  package_dir=PACKAGE_DIRECTORIES,
  package_data=PACKAGE_DATA,
  install_requires=INSTALL_REQUIRES,
  setup_requires=SETUP_REQUIRES,
  cmdclass=COMMAND_CLASS,
  tests_require=TESTS_REQUIRE,
  test_suite=TEST_SUITE,
  test_loader=TEST_LOADER,
  test_runner=TEST_RUNNER,
)
