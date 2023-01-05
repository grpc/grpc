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

import subprocess
import sys
import tempfile
import time
import threading
import unittest

from grpc._cython import cygrpc

from tests.fork import methods
_COUNTER = 0
def mark():
    global _COUNTER
    import sys; sys.stderr.write("BBBBBBBBBBBBBBBBBBBBB {}\n".format(_COUNTER)); sys.stderr.flush()
    _COUNTER += 1

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
    sys.stderr.write("Running test case.\\n"); sys.stderr.flush()
    methods.TestCase.%s.run_test({
      'server_host': 'localhost',
      'server_port': %d,
      'use_tls': False
    })
    sys.stderr.write("Ran test case.\\n"); sys.stderr.flush()
"""
_SUBPROCESS_TIMEOUT_S = 30


@unittest.skipUnless(
    sys.platform.startswith("linux"),
    "not supported on windows, and fork+exec networking blocked on mac")
class ForkInteropTest(unittest.TestCase):

    def setUp(self):
        self._port = None
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
        self._streams = tuple(tempfile.TemporaryFile() for _ in range(2))
        self._server_process = subprocess.Popen(
            [sys.executable, '-c', start_server_script],
            stdout=self._streams[0],
            stderr=self._streams[1])
        timer = threading.Timer(_SUBPROCESS_TIMEOUT_S,
                                self._server_process.kill)
        interval_secs = 2.0
        cumulative_secs = 0.0
        try:
            timer.start()
            while cumulative_secs < _SUBPROCESS_TIMEOUT_S:
                self._streams[0].seek(0)
                s = self._streams[0].readline()
                if s:
                    self._port = int(s)
                    break
                time.sleep(interval_secs)
                cumulative_secs += interval_secs

            if self._port is None:
                # Timeout
                self._streams[0].seek(0)
                sys.stderr.write("Server STDOUT:\n{}\n".format(self._streams[0].read()))
                self._streams[1].seek(0)
                sys.stderr.write("Server STDERR:\n{}\n".format(self._streams[1].read()))
                sys.stderr.flush()
                raise Exception("Failed to get port from server.")
        except ValueError:
            raise Exception('Failed to get port from server')
        finally:
            timer.cancel()

    def testConnectivityWatch(self):
        self._verifyTestCase(methods.TestCase.CONNECTIVITY_WATCH)

    # def testCloseChannelBeforeFork(self):
    #     self._verifyTestCase(methods.TestCase.CLOSE_CHANNEL_BEFORE_FORK)

    # def testAsyncUnarySameChannel(self):
    #     self._verifyTestCase(methods.TestCase.ASYNC_UNARY_SAME_CHANNEL)

    # def testAsyncUnaryNewChannel(self):
    #     self._verifyTestCase(methods.TestCase.ASYNC_UNARY_NEW_CHANNEL)

    # def testBlockingUnarySameChannel(self):
    #     self._verifyTestCase(methods.TestCase.BLOCKING_UNARY_SAME_CHANNEL)

    # def testBlockingUnaryNewChannel(self):
    #     self._verifyTestCase(methods.TestCase.BLOCKING_UNARY_NEW_CHANNEL)

    # def testInProgressBidiContinueCall(self):
    #     self._verifyTestCase(methods.TestCase.IN_PROGRESS_BIDI_CONTINUE_CALL)

    # def testInProgressBidiSameChannelAsyncCall(self):
    #     self._verifyTestCase(
    #         methods.TestCase.IN_PROGRESS_BIDI_SAME_CHANNEL_ASYNC_CALL)

    # def testInProgressBidiSameChannelBlockingCall(self):
    #     self._verifyTestCase(
    #         methods.TestCase.IN_PROGRESS_BIDI_SAME_CHANNEL_BLOCKING_CALL)

    # def testInProgressBidiNewChannelAsyncCall(self):
    #     self._verifyTestCase(
    #         methods.TestCase.IN_PROGRESS_BIDI_NEW_CHANNEL_ASYNC_CALL)

    # def testInProgressBidiNewChannelBlockingCall(self):
    #     self._verifyTestCase(
    #         methods.TestCase.IN_PROGRESS_BIDI_NEW_CHANNEL_BLOCKING_CALL)

    def tearDown(self):
        self._server_process.kill()
        for stream in self._streams:
            stream.close()

    def _verifyTestCase(self, test_case):
        script = _CLIENT_FORK_SCRIPT_TEMPLATE % (test_case.name, self._port)
        streams = tuple(tempfile.TemporaryFile() for _ in range(2))
        process = subprocess.Popen([sys.executable, '-c', script],
                                   stdout=streams[0],
                                   stderr=streams[1])
        try:
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
                (process.returncode, outputs[0].decode('ascii'), outputs[1].decode('ascii')))
        finally:
            for stream in streams:
                stream.close()


if __name__ == '__main__':
    unittest.main(verbosity=2)
