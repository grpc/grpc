# Copyright 2018 gRPC authors.
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
"""Client-side fork interop tests as a unit test."""

import unittest
import os
import subprocess
import sys
from grpc._cython import cygrpc
from tests.fork import methods


@unittest.skipIf(
    sys.platform.startswith("win"), "fork is not supported on windows")
class ForkInteropTest(unittest.TestCase):

    @classmethod
    def setUpClass(self):
        start_server_script = """if True:
            import sys
            import time

            import grpc
            from src.proto.grpc.testing import test_pb2_grpc
            from tests.interop import methods as interop_methods
            from tests.unit import test_common

            server = test_common.test_server()
            test_pb2_grpc.add_TestServiceServicer_to_server(
                interop_methods.TestService(), server)
            port = server.add_insecure_port('[::]:0')
            server.start()
            print(port)
            sys.stdout.flush()
            while True:
                time.sleep(1)
        """
        self._process = subprocess.Popen(
            [sys.executable, '-c', start_server_script],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE)
        port = int(self._process.stdout.readline())
        self._channel_args = {
            'server_host': 'localhost',
            'server_port': port,
            'use_tls': False
        }
        cygrpc._GRPC_ENABLE_FORK_SUPPORT = True
        self._prev_poller = os.environ.get('GRPC_POLL_STRATEGY', None)
        if sys.platform.startswith("linux"):
            os.environ['GRPC_POLL_STRATEGY'] = 'epoll1'
        else:
            os.environ['GRPC_POLL_STRATEGY'] = 'poll'

    def testConnectivityWatch(self):
        methods.TestCase.CONNECTIVITY_WATCH.run_test(self._channel_args)

    def testCloseChannelBeforeFork(self):
        methods.TestCase.CLOSE_CHANNEL_BEFORE_FORK.run_test(self._channel_args)

    def testAsyncUnarySameChannel(self):
        methods.TestCase.ASYNC_UNARY_SAME_CHANNEL.run_test(self._channel_args)

    def testAsyncUnaryNewChannel(self):
        methods.TestCase.ASYNC_UNARY_NEW_CHANNEL.run_test(self._channel_args)

    def testBlockingUnarySameChannel(self):
        methods.TestCase.BLOCKING_UNARY_SAME_CHANNEL.run_test(
            self._channel_args)

    def testBlockingUnaryNewChannel(self):
        methods.TestCase.BLOCKING_UNARY_NEW_CHANNEL.run_test(self._channel_args)

    def testInProgressBidiContinueCall(self):
        methods.TestCase.IN_PROGRESS_BIDI_CONTINUE_CALL.run_test(
            self._channel_args)

    def testInProgressBidiSameChannelAsyncCall(self):
        methods.TestCase.IN_PROGRESS_BIDI_SAME_CHANNEL_ASYNC_CALL.run_test(
            self._channel_args)

    def testInProgressBidiSameChannelBlockingCall(self):
        methods.TestCase.IN_PROGRESS_BIDI_SAME_CHANNEL_BLOCKING_CALL.run_test(
            self._channel_args)

    def testInProgressBidiNewChannelAsyncCall(self):
        methods.TestCase.IN_PROGRESS_BIDI_NEW_CHANNEL_ASYNC_CALL.run_test(
            self._channel_args)

    def testInProgressBidiNewChannelBlockingCall(self):
        methods.TestCase.IN_PROGRESS_BIDI_NEW_CHANNEL_BLOCKING_CALL.run_test(
            self._channel_args)


    @classmethod
    def tearDownClass(self):
        cygrpc._GRPC_ENABLE_FORK_SUPPORT = False
        if self._prev_poller is None:
            del os.environ['GRPC_POLL_STRATEGY']
        else:
            os.environ['GRPC_POLL_STRATEGY'] = self._prev_poller
        self._process.kill()


if __name__ == '__main__':
    unittest.main(verbosity=2)
