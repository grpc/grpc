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
"""Provides setuptools command classes for the gRPC Python setup process."""

import glob
import os
import os.path
import platform
import re
import shutil
import sys

import setuptools
from setuptools import errors as _errors
from setuptools.command import build_ext
from setuptools.command import build_py
from setuptools.command import easy_install
from setuptools.command import install
from setuptools.command import test

PYTHON_STEM = os.path.dirname(os.path.abspath(__file__))
GRPC_STEM = os.path.abspath(PYTHON_STEM + "../../../../")
GRPC_PROTO_STEM = os.path.join(GRPC_STEM, "src", "proto")
PROTO_STEM = os.path.join(PYTHON_STEM, "src", "proto")
PYTHON_PROTO_TOP_LEVEL = os.path.join(PYTHON_STEM, "src")


class CommandError(object):
    pass


class GatherProto(setuptools.Command):
    description = "gather proto dependencies"
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
            path = os.path.join(root, "__init__.py")
            open(path, "a").close()


class BuildPy(build_py.build_py):
    """Custom project build command."""

    def run(self):
        try:
            self.run_command("build_package_protos")
        except CommandError as error:
            sys.stderr.write("warning: %s\n" % error.message)
        build_py.build_py.run(self)


class TestLite(setuptools.Command):
    """Command to run tests without fetching or building anything."""

    description = "run tests without fetching or building anything."
    user_options = []

    def initialize_options(self):
        pass

    def finalize_options(self):
        # distutils requires this override.
        pass

    def run(self):
        import tests

        loader = tests.Loader()
        loader.loadTestsFromNames(["tests"])
        runner = tests.Runner(dedicated_threads=True)
        result = runner.run(loader.suite)
        if not result.wasSuccessful():
            sys.exit("Test failure")


class TestPy3Only(setuptools.Command):
    """Command to run tests for Python 3+ features.

    This does not include asyncio tests, which are housed in a separate
    directory.
    """

    description = "run tests for py3+ features"
    user_options = []

    def initialize_options(self):
        pass

    def finalize_options(self):
        pass

    def run(self):
        import tests

        loader = tests.Loader()
        loader.loadTestsFromNames(["tests_py3_only"])
        runner = tests.Runner()
        result = runner.run(loader.suite)
        if not result.wasSuccessful():
            sys.exit("Test failure")


class TestAio(setuptools.Command):
    """Command to run aio tests without fetching or building anything."""

    description = "run aio tests without fetching or building anything."
    user_options = []

    def initialize_options(self):
        pass

    def finalize_options(self):
        pass

    def run(self):
        import tests

        loader = tests.Loader()
        loader.loadTestsFromNames(["tests_aio"])
        # Even without dedicated threads, the framework will somehow spawn a
        # new thread for tests to run upon. New thread doesn't have event loop
        # attached by default, so initialization is needed.
        runner = tests.Runner(dedicated_threads=False)
        result = runner.run(loader.suite)
        if not result.wasSuccessful():
            sys.exit("Test failure")


class RunInterop(test.test):
    description = "run interop test client/server"
    user_options = [
        ("args=", None, "pass-thru arguments for the client/server"),
        ("client", None, "flag indicating to run the client"),
        ("server", None, "flag indicating to run the server"),
        ("use-asyncio", None, "flag indicating to run the asyncio stack"),
    ]

    def initialize_options(self):
        self.args = ""
        self.client = False
        self.server = False
        self.use_asyncio = False

    def finalize_options(self):
        if self.client and self.server:
            raise _errors.OptionError(
                "you may only specify one of client or server"
            )

    def run(self):
        if self.client:
            self.run_client()
        elif self.server:
            self.run_server()

    def run_server(self):
        # We import here to ensure that our setuptools parent has had a chance to
        # edit the Python system path.
        if self.use_asyncio:
            import asyncio

            from tests_aio.interop import server

            sys.argv[1:] = self.args.split()
            args = server.parse_interop_server_arguments(sys.argv)
            asyncio.get_event_loop().run_until_complete(server.serve(args))
        else:
            from tests.interop import server

            sys.argv[1:] = self.args.split()
            server.serve(server.parse_interop_server_arguments(sys.argv))

    def run_client(self):
        # We import here to ensure that our setuptools parent has had a chance to
        # edit the Python system path.
        from tests.interop import client

        sys.argv[1:] = self.args.split()
        client.test_interoperability(client.parse_interop_client_args(sys.argv))


class RunFork(test.test):
    description = "run fork test client"
    user_options = [("args=", "a", "pass-thru arguments for the client")]

    def initialize_options(self):
        self.args = ""

    def finalize_options(self):
        # distutils requires this override.
        pass

    def run(self):
        # We import here to ensure that our setuptools parent has had a chance to
        # edit the Python system path.
        from tests.fork import client

        sys.argv[1:] = self.args.split()
        client.test_fork()
