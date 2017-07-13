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
"""Provides distutils command classes for the gRPC Python setup process."""

from distutils import errors as _errors
import glob
import os
import os.path
import platform
import re
import shutil
import subprocess
import sys
import traceback

import setuptools
from setuptools.command import build_ext
from setuptools.command import build_py
from setuptools.command import easy_install
from setuptools.command import install
from setuptools.command import test

PYTHON_STEM = os.path.dirname(os.path.abspath(__file__))
GRPC_STEM = os.path.abspath(PYTHON_STEM + '../../../../')
GRPC_PROTO_STEM = os.path.join(GRPC_STEM, 'src', 'proto')
PROTO_STEM = os.path.join(PYTHON_STEM, 'src', 'proto')
PYTHON_PROTO_TOP_LEVEL = os.path.join(PYTHON_STEM, 'src')


class CommandError(object):
    pass


class GatherProto(setuptools.Command):

    description = 'gather proto dependencies'
    user_options = []

    def initialize_options(self):
        pass

    def finalize_options(self):
        pass

    def run(self):
        # TODO(atash) ensure that we're running from the repository directory when
        # this command is used
        try:
            shutil.rmtree(PROTO_STEM)
        except Exception as error:
            # We don't care if this command fails
            pass
        shutil.copytree(GRPC_PROTO_STEM, PROTO_STEM)
        for root, _, _ in os.walk(PYTHON_PROTO_TOP_LEVEL):
            path = os.path.join(root, '__init__.py')
            open(path, 'a').close()


class BuildProtoModules(setuptools.Command):
    """Command to generate project *_pb2.py modules from proto files."""

    description = 'build protobuf modules'
    user_options = [
        ('include=', None, 'path patterns to include in protobuf generation'),
        ('exclude=', None, 'path patterns to exclude from protobuf generation')
    ]

    def initialize_options(self):
        self.exclude = None
        self.include = r'.*\.proto$'

    def finalize_options(self):
        pass

    def run(self):
        import grpc_tools.protoc as protoc

        include_regex = re.compile(self.include)
        exclude_regex = re.compile(self.exclude) if self.exclude else None
        paths = []
        for walk_root, directories, filenames in os.walk(PROTO_STEM):
            for filename in filenames:
                path = os.path.join(walk_root, filename)
                if include_regex.match(path) and not (
                        exclude_regex and exclude_regex.match(path)):
                    paths.append(path)

        # TODO(kpayson): It would be nice to do this in a batch command,
        # but we currently have name conflicts in src/proto
        for path in paths:
            command = [
                'grpc_tools.protoc',
                '-I {}'.format(PROTO_STEM),
                '--python_out={}'.format(PROTO_STEM),
                '--grpc_python_out={}'.format(PROTO_STEM),
            ] + [path]
            if protoc.main(command) != 0:
                sys.stderr.write(
                    'warning: Command:\n{}\nFailed'.format(command))

        # Generated proto directories dont include __init__.py, but
        # these are needed for python package resolution
        for walk_root, _, _ in os.walk(PROTO_STEM):
            path = os.path.join(walk_root, '__init__.py')
            open(path, 'a').close()


class BuildPy(build_py.build_py):
    """Custom project build command."""

    def run(self):
        try:
            self.run_command('build_package_protos')
        except CommandError as error:
            sys.stderr.write('warning: %s\n' % error.message)
        build_py.build_py.run(self)


class TestLite(setuptools.Command):
    """Command to run tests without fetching or building anything."""

    description = 'run tests without fetching or building anything.'
    user_options = []

    def initialize_options(self):
        pass

    def finalize_options(self):
        # distutils requires this override.
        pass

    def run(self):
        self._add_eggs_to_path()

        import tests
        loader = tests.Loader()
        loader.loadTestsFromNames(['tests'])
        runner = tests.Runner()
        result = runner.run(loader.suite)
        if not result.wasSuccessful():
            sys.exit('Test failure')

    def _add_eggs_to_path(self):
        """Fetch install and test requirements"""
        self.distribution.fetch_build_eggs(self.distribution.install_requires)
        self.distribution.fetch_build_eggs(self.distribution.tests_require)


class RunInterop(test.test):

    description = 'run interop test client/server'
    user_options = [('args=', 'a', 'pass-thru arguments for the client/server'),
                    ('client', 'c', 'flag indicating to run the client'),
                    ('server', 's', 'flag indicating to run the server')]

    def initialize_options(self):
        self.args = ''
        self.client = False
        self.server = False

    def finalize_options(self):
        if self.client and self.server:
            raise _errors.DistutilsOptionError(
                'you may only specify one of client or server')

    def run(self):
        if self.distribution.install_requires:
            self.distribution.fetch_build_eggs(
                self.distribution.install_requires)
        if self.distribution.tests_require:
            self.distribution.fetch_build_eggs(self.distribution.tests_require)
        if self.client:
            self.run_client()
        elif self.server:
            self.run_server()

    def run_server(self):
        # We import here to ensure that our setuptools parent has had a chance to
        # edit the Python system path.
        from tests.interop import server
        sys.argv[1:] = self.args.split()
        server.serve()

    def run_client(self):
        # We import here to ensure that our setuptools parent has had a chance to
        # edit the Python system path.
        from tests.interop import client
        sys.argv[1:] = self.args.split()
        client.test_interoperability()
