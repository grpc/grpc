# Copyright 2019 gRPC authors.
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

import six
import subprocess
import sys
import tempfile
import threading
import unittest
from grpc._cython import cygrpc
from tests.fork import methods

# New instance of multiprocessing.Process using fork without exec can and will
# freeze if the Python process has any other threads running. This includes the
# additional thread spawned by our _runner.py class. So in order to test our
# compatibility with multiprocessing, we first fork+exec a new process to ensure
# we don't have any conflicting background threads.
_CLIENT_FORK_SCRIPT_TEMPLATE = """if True:
    import os
    import sys
    from grpc._cython import cygrpc
    from tests.fork import methods

    cygrpc._GRPC_ENABLE_FORK_SUPPORT = True
    os.environ['GRPC_POLL_STRATEGY'] = 'epoll1'
    methods.TestCase.%s.run_test({
      'server_host': 'localhost',
      'server_port': %d,
      'use_tls': False
    })
"""
_SUBPROCESS_TIMEOUT_S = 30


@unittest.skipUnless(
    sys.platform.startswith("linux"),
    "not supported on windows, and fork+exec networking blocked on mac")
@unittest.skipUnless(six.PY2, "https://github.com/grpc/grpc/issues/18075")
class ForkInteropTest(unittest.TestCase):

    def setUp(self):
        start_server_script = """if True:
            import sys
            import time

            import grpc
            from src.proto.grpc.testing import test_pb2_grpc
            from tests.interop import service as interop_service
            from tests.unit import test_common

            server = test_common.test_server()
            test_pb2_grpc.add_TestServiceServicer_to_server(
                interop_service.TestService(), server)
            port = server.add_insecure_port('[::]:0')
            server.start()
            print(port)
            sys.stdout.flush()
            while True:
                time.sleep(1)
        """
        streams = tuple(tempfile.TemporaryFile() for _ in range(2))
        self._server_process = subprocess.Popen(
            [sys.executable, '-c', start_server_script],
            stdout=streams[0],
            stderr=streams[1])
        timer = threading.Timer(_SUBPROCESS_TIMEOUT_S,
                                self._server_process.kill)
        try:
            timer.start()
            while True:
                streams[0].seek(0)
                s = streams[0].readline()
                if not s:
                    continue
                else:
                    self._port = int(s)
                    break
        except ValueError:
            raise Exception('Failed to get port from server')
        finally:
            timer.cancel()

    def testConnectivityWatch(self):
        self._verifyTestCase(methods.TestCase.CONNECTIVITY_WATCH)

    def testCloseChannelBeforeFork(self):
        self._verifyTestCase(methods.TestCase.CLOSE_CHANNEL_BEFORE_FORK)

    def testAsyncUnarySameChannel(self):
        self._verifyTestCase(methods.TestCase.ASYNC_UNARY_SAME_CHANNEL)

    def testAsyncUnaryNewChannel(self):
        self._verifyTestCase(methods.TestCase.ASYNC_UNARY_NEW_CHANNEL)

    def testBlockingUnarySameChannel(self):
        self._verifyTestCase(methods.TestCase.BLOCKING_UNARY_SAME_CHANNEL)

    def testBlockingUnaryNewChannel(self):
        self._verifyTestCase(methods.TestCase.BLOCKING_UNARY_NEW_CHANNEL)

    def testInProgressBidiContinueCall(self):
        self._verifyTestCase(methods.TestCase.IN_PROGRESS_BIDI_CONTINUE_CALL)

    def testInProgressBidiSameChannelAsyncCall(self):
        self._verifyTestCase(
            methods.TestCase.IN_PROGRESS_BIDI_SAME_CHANNEL_ASYNC_CALL)

    def testInProgressBidiSameChannelBlockingCall(self):
        self._verifyTestCase(
            methods.TestCase.IN_PROGRESS_BIDI_SAME_CHANNEL_BLOCKING_CALL)

    def testInProgressBidiNewChannelAsyncCall(self):
        self._verifyTestCase(
            methods.TestCase.IN_PROGRESS_BIDI_NEW_CHANNEL_ASYNC_CALL)

    def testInProgressBidiNewChannelBlockingCall(self):
        self._verifyTestCase(
            methods.TestCase.IN_PROGRESS_BIDI_NEW_CHANNEL_BLOCKING_CALL)

    def tearDown(self):
        self._server_process.kill()

    def _verifyTestCase(self, test_case):
        script = _CLIENT_FORK_SCRIPT_TEMPLATE % (test_case.name, self._port)
        streams = tuple(tempfile.TemporaryFile() for _ in range(2))
        process = subprocess.Popen([sys.executable, '-c', script],
                                   stdout=streams[0],
                                   stderr=streams[1])
        timer = threading.Timer(_SUBPROCESS_TIMEOUT_S, process.kill)
        timer.start()
        process.wait()
        timer.cancel()
        outputs = []
        for stream in streams:
            stream.seek(0)
            outputs.append(stream.read())
        self.assertEqual(
            0, process.returncode,
            'process failed with exit code %d (stdout: "%s", stderr: "%s")' %
            (process.returncode, outputs[0], outputs[1]))


if __name__ == '__main__':
    unittest.main(verbosity=2)
