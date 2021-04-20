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
import sys

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
        runner = tests.Runner(dedicated_threads=True)
        result = runner.run(loader.suite)
        if not result.wasSuccessful():
            sys.exit('Test failure')

    def _add_eggs_to_path(self):
        """Fetch install and test requirements"""
        self.distribution.fetch_build_eggs(self.distribution.install_requires)
        self.distribution.fetch_build_eggs(self.distribution.tests_require)


class TestPy3Only(setuptools.Command):
    """Command to run tests for Python 3+ features.

    This does not include asyncio tests, which are housed in a separate
    directory.
    """

    description = 'run tests for py3+ features'
    user_options = []

    def initialize_options(self):
        pass

    def finalize_options(self):
        pass

    def run(self):
        self._add_eggs_to_path()
        import tests
        loader = tests.Loader()
        loader.loadTestsFromNames(['tests_py3_only'])
        runner = tests.Runner()
        result = runner.run(loader.suite)
        if not result.wasSuccessful():
            sys.exit('Test failure')

    def _add_eggs_to_path(self):
        self.distribution.fetch_build_eggs(self.distribution.install_requires)
        self.distribution.fetch_build_eggs(self.distribution.tests_require)


class TestAio(setuptools.Command):
    """Command to run aio tests without fetching or building anything."""

    description = 'run aio tests without fetching or building anything.'
    user_options = []

    def initialize_options(self):
        pass

    def finalize_options(self):
        pass

    def run(self):
        self._add_eggs_to_path()

        import tests
        loader = tests.Loader()
        loader.loadTestsFromNames(['tests_aio'])
        # Even without dedicated threads, the framework will somehow spawn a
        # new thread for tests to run upon. New thread doesn't have event loop
        # attached by default, so initialization is needed.
        runner = tests.Runner(dedicated_threads=False)
        result = runner.run(loader.suite)
        if not result.wasSuccessful():
            sys.exit('Test failure')

    def _add_eggs_to_path(self):
        """Fetch install and test requirements"""
        self.distribution.fetch_build_eggs(self.distribution.install_requires)
        self.distribution.fetch_build_eggs(self.distribution.tests_require)


class TestGevent(setuptools.Command):
    """Command to run tests w/gevent."""

    BANNED_TESTS = (
        # Fork support is not compatible with gevent
        'fork._fork_interop_test.ForkInteropTest',
        # These tests send a lot of RPCs and are really slow on gevent.  They will
        # eventually succeed, but need to dig into performance issues.
        'unit._cython._no_messages_server_completion_queue_per_call_test.Test.test_rpcs',
        'unit._cython._no_messages_single_server_completion_queue_test.Test.test_rpcs',
        'unit._compression_test',
        # TODO(https://github.com/grpc/grpc/issues/16890) enable this test
        'unit._cython._channel_test.ChannelTest.test_multiple_channels_lonely_connectivity',
        # I have no idea why this doesn't work in gevent, but it shouldn't even be
        # using the c-core
        'testing._client_test.ClientTest.test_infinite_request_stream_real_time',
        # TODO(https://github.com/grpc/grpc/issues/15743) enable this test
        'unit._session_cache_test.SSLSessionCacheTest.testSSLSessionCacheLRU',
        # TODO(https://github.com/grpc/grpc/issues/14789) enable this test
        'unit._server_ssl_cert_config_test',
        # TODO(https://github.com/grpc/grpc/issues/14901) enable this test
        'protoc_plugin._python_plugin_test.PythonPluginTest',
        'protoc_plugin._python_plugin_test.SimpleStubsPluginTest',
        # Beta API is unsupported for gevent
        'protoc_plugin.beta_python_plugin_test',
        'unit.beta._beta_features_test',
        # TODO(https://github.com/grpc/grpc/issues/15411) unpin gevent version
        # This test will stuck while running higher version of gevent
        'unit._auth_context_test.AuthContextTest.testSessionResumption',
        # TODO(https://github.com/grpc/grpc/issues/15411) enable these tests
        'unit._channel_ready_future_test.ChannelReadyFutureTest.test_immediately_connectable_channel_connectivity',
        "unit._cython._channel_test.ChannelTest.test_single_channel_lonely_connectivity",
        'unit._exit_test.ExitTest.test_in_flight_unary_unary_call',
        'unit._exit_test.ExitTest.test_in_flight_unary_stream_call',
        'unit._exit_test.ExitTest.test_in_flight_stream_unary_call',
        'unit._exit_test.ExitTest.test_in_flight_stream_stream_call',
        'unit._exit_test.ExitTest.test_in_flight_partial_unary_stream_call',
        'unit._exit_test.ExitTest.test_in_flight_partial_stream_unary_call',
        'unit._exit_test.ExitTest.test_in_flight_partial_stream_stream_call',
        # TODO(https://github.com/grpc/grpc/issues/18980): Reenable.
        'unit._signal_handling_test.SignalHandlingTest',
        'unit._metadata_flags_test',
        'health_check._health_servicer_test.HealthServicerTest.test_cancelled_watch_removed_from_watch_list',
        # TODO(https://github.com/grpc/grpc/issues/17330) enable these three tests
        'channelz._channelz_servicer_test.ChannelzServicerTest.test_many_subchannels',
        'channelz._channelz_servicer_test.ChannelzServicerTest.test_many_subchannels_and_sockets',
        'channelz._channelz_servicer_test.ChannelzServicerTest.test_streaming_rpc',
        # TODO(https://github.com/grpc/grpc/issues/15411) enable this test
        'unit._cython._channel_test.ChannelTest.test_negative_deadline_connectivity',
        # TODO(https://github.com/grpc/grpc/issues/15411) enable this test
        'unit._local_credentials_test.LocalCredentialsTest',
        # TODO(https://github.com/grpc/grpc/issues/22020) LocalCredentials
        # aren't supported with custom io managers.
        'unit._contextvars_propagation_test',
        'testing._time_test.StrictRealTimeTest',
    )
    BANNED_WINDOWS_TESTS = (
        # TODO(https://github.com/grpc/grpc/pull/15411) enable this test
        'unit._dns_resolver_test.DNSResolverTest.test_connect_loopback',
        # TODO(https://github.com/grpc/grpc/pull/15411) enable this test
        'unit._server_test.ServerTest.test_failed_port_binding_exception',
    )
    BANNED_MACOS_TESTS = (
        # TODO(https://github.com/grpc/grpc/issues/15411) enable this test
        'unit._dynamic_stubs_test.DynamicStubTest',)
    description = 'run tests with gevent.  Assumes grpc/gevent are installed'
    user_options = []

    def initialize_options(self):
        pass

    def finalize_options(self):
        # distutils requires this override.
        pass

    def run(self):
        from gevent import monkey
        monkey.patch_all()

        import tests

        import grpc.experimental.gevent
        grpc.experimental.gevent.init_gevent()

        import gevent

        import tests
        loader = tests.Loader()
        loader.loadTestsFromNames(['tests'])
        runner = tests.Runner()
        if sys.platform == 'win32':
            runner.skip_tests(self.BANNED_TESTS + self.BANNED_WINDOWS_TESTS)
        elif sys.platform == 'darwin':
            runner.skip_tests(self.BANNED_TESTS + self.BANNED_MACOS_TESTS)
        else:
            runner.skip_tests(self.BANNED_TESTS)
        result = gevent.spawn(runner.run, loader.suite)
        result.join()
        if not result.value.wasSuccessful():
            sys.exit('Test failure')


class RunInterop(test.test):

    description = 'run interop test client/server'
    user_options = [
        ('args=', None, 'pass-thru arguments for the client/server'),
        ('client', None, 'flag indicating to run the client'),
        ('server', None, 'flag indicating to run the server'),
        ('use-asyncio', None, 'flag indicating to run the asyncio stack')
    ]

    def initialize_options(self):
        self.args = ''
        self.client = False
        self.server = False
        self.use_asyncio = False

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
        if self.use_asyncio:
            import asyncio
            from tests_aio.interop import server
            sys.argv[1:] = self.args.split()
            asyncio.get_event_loop().run_until_complete(server.serve())
        else:
            from tests.interop import server
            sys.argv[1:] = self.args.split()
            server.serve()

    def run_client(self):
        # We import here to ensure that our setuptools parent has had a chance to
        # edit the Python system path.
        from tests.interop import client
        sys.argv[1:] = self.args.split()
        client.test_interoperability()


class RunFork(test.test):

    description = 'run fork test client'
    user_options = [('args=', 'a', 'pass-thru arguments for the client')]

    def initialize_options(self):
        self.args = ''

    def finalize_options(self):
        # distutils requires this override.
        pass

    def run(self):
        if self.distribution.install_requires:
            self.distribution.fetch_build_eggs(
                self.distribution.install_requires)
        if self.distribution.tests_require:
            self.distribution.fetch_build_eggs(self.distribution.tests_require)
        # We import here to ensure that our setuptools parent has had a chance to
        # edit the Python system path.
        from tests.fork import client
        sys.argv[1:] = self.args.split()
        client.test_fork()
