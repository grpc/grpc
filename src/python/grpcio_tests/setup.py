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

import multiprocessing
import os
import sys

# Manually insert the source directory into the Python path for local module
# imports to succeed
sys.path.insert(0, os.path.abspath("."))

import grpc_tools.command
import setuptools

import commands
import grpc_version
import python_version

INSTALL_REQUIRES = (
    "coverage>=7.9.0",
    "grpcio>={version}".format(version=grpc_version.VERSION),
    "grpcio-channelz>={version}".format(version=grpc_version.VERSION),
    "grpcio-status>={version}".format(version=grpc_version.VERSION),
    "grpcio-tools>={version}".format(version=grpc_version.VERSION),
    "grpcio-health-checking>={version}".format(version=grpc_version.VERSION),
    "grpcio-observability>={version}".format(version=grpc_version.VERSION),
    "xds-protos>={version}".format(version=grpc_version.VERSION),
    "oauth2client>=1.4.7",
    "protobuf>=6.31.1,<7.0.0",
    "google-auth>=1.17.2",
    "requests>=2.14.2",
    "absl-py>=1.4.0",
)

COMMAND_CLASS = {
    # Run `preprocess` *before* doing any packaging!
    "preprocess": commands.GatherProto,
    "build_package_protos": commands.BuildPackageProtos,
    "build_py": commands.BuildPy,
    "run_fork": commands.RunFork,
    "run_interop": commands.RunInterop,
    "test_lite": commands.TestLite,
    "test_aio": commands.TestAio,
    "test_py3_only": commands.TestPy3Only,
}

TEST_SUITE = "tests"
TEST_LOADER = "tests:Loader"
TEST_RUNNER = "tests:Runner"
TESTS_REQUIRE = INSTALL_REQUIRES

CLASSIFIERS = [
    "Programming Language :: Python",
    "Programming Language :: Python :: 3",
]

if __name__ == "__main__":
    multiprocessing.freeze_support()
    setuptools.setup(
        install_requires=INSTALL_REQUIRES,
        cmdclass=COMMAND_CLASS,
        classifiers=CLASSIFIERS,
        python_requires=f">={python_version.MIN_PYTHON_VERSION}",
        tests_require=TESTS_REQUIRE,
        test_suite=TEST_SUITE,
        test_loader=TEST_LOADER,
        test_runner=TEST_RUNNER,
    )
