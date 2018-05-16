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
"""A setup module for the gRPC Python package."""

import os
import os.path
import sys

import setuptools

import grpc_tools.command

PY3 = sys.version_info.major == 3

# Ensure we're in the proper directory whether or not we're being used by pip.
os.chdir(os.path.dirname(os.path.abspath(__file__)))

# Break import-style to ensure we can actually find our in-repo dependencies.
import commands
import grpc_version

LICENSE = 'Apache License 2.0'

PACKAGE_DIRECTORIES = {
    '': '.',
}

INSTALL_REQUIRES = (
    'coverage>=4.0', 'enum34>=1.0.4',
    'grpcio>={version}'.format(version=grpc_version.VERSION),
    'grpcio-tools>={version}'.format(version=grpc_version.VERSION),
    'grpcio-health-checking>={version}'.format(version=grpc_version.VERSION),
    'oauth2client>=1.4.7', 'protobuf>=3.5.2.post1', 'six>=1.10',
    'google-auth>=1.0.0', 'requests>=2.14.2')

if not PY3:
    INSTALL_REQUIRES += ('futures>=2.2.0',)

COMMAND_CLASS = {
    # Run `preprocess` *before* doing any packaging!
    'preprocess': commands.GatherProto,
    'build_package_protos': grpc_tools.command.BuildPackageProtos,
    'build_py': commands.BuildPy,
    'run_interop': commands.RunInterop,
    'test_lite': commands.TestLite,
    'test_gevent': commands.TestGevent,
}

PACKAGE_DATA = {
    'tests.interop': [
        'credentials/ca.pem',
        'credentials/server1.key',
        'credentials/server1.pem',
    ],
    'tests.protoc_plugin.protos.invocation_testing': [
        'same.proto',
    ],
    'tests.protoc_plugin.protos.invocation_testing.split_messages': [
        'messages.proto',
    ],
    'tests.protoc_plugin.protos.invocation_testing.split_services': [
        'services.proto',
    ],
    'tests.testing.proto': [
        'requests.proto',
        'services.proto',
    ],
    'tests.unit': [
        'credentials/ca.pem',
        'credentials/server1.key',
        'credentials/server1.pem',
    ],
    'tests': ['tests.json'],
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
    cmdclass=COMMAND_CLASS,
    tests_require=TESTS_REQUIRE,
    test_suite=TEST_SUITE,
    test_loader=TEST_LOADER,
    test_runner=TEST_RUNNER,
)
