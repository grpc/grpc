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

@unittest.skipUnless(
    sys.platform.startswith("linux"), "fork is not supported on windows, and connections to the fork+exec server process is blocked on mac")
class ForkInteropTest(unittest.TestCase):

    def setUp(self):
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
        self._port = int(self._process.stdout.readline())

    def testConnectivityWatch(self):
        test_case = "CONNECTIVITY_WATCH"
        self._verifyScriptSucceeds(_CLIENT_FORK_SCRIPT_TEMPLATE % (test_case, self._port))

    def testCloseChannelBeforeFork(self):
        test_case = "CLOSE_CHANNEL_BEFORE_FORK"
        self._verifyScriptSucceeds(_CLIENT_FORK_SCRIPT_TEMPLATE % (test_case, self._port))

    def testAsyncUnarySameChannel(self):
        test_case = "ASYNC_UNARY_SAME_CHANNEL"
        self._verifyScriptSucceeds(_CLIENT_FORK_SCRIPT_TEMPLATE % (test_case, self._port))

    def testAsyncUnaryNewChannel(self):
        test_case = "ASYNC_UNARY_NEW_CHANNEL"
        self._verifyScriptSucceeds(_CLIENT_FORK_SCRIPT_TEMPLATE % (test_case, self._port))

    def testBlockingUnarySameChannel(self):
        test_case = "BLOCKING_UNARY_SAME_CHANNEL"
        self._verifyScriptSucceeds(_CLIENT_FORK_SCRIPT_TEMPLATE % (test_case, self._port))

    def testBlockingUnaryNewChannel(self):
        test_case = "BLOCKING_UNARY_NEW_CHANNEL"
        self._verifyScriptSucceeds(_CLIENT_FORK_SCRIPT_TEMPLATE % (test_case, self._port))

    def testInProgressBidiContinueCall(self):
        test_case = "IN_PROGRESS_BIDI_CONTINUE_CALL"
        self._verifyScriptSucceeds(_CLIENT_FORK_SCRIPT_TEMPLATE % (test_case, self._port))

    def testInProgressBidiSameChannelAsyncCall(self):
        test_case = "IN_PROGRESS_BIDI_SAME_CHANNEL_ASYNC_CALL"
        self._verifyScriptSucceeds(_CLIENT_FORK_SCRIPT_TEMPLATE % (test_case, self._port))

    def testInProgressBidiSameChannelBlockingCall(self):
        test_case = "IN_PROGRESS_BIDI_SAME_CHANNEL_BLOCKING_CALL"
        self._verifyScriptSucceeds(_CLIENT_FORK_SCRIPT_TEMPLATE % (test_case, self._port))

    def testInProgressBidiNewChannelAsyncCall(self):
        test_case = "IN_PROGRESS_BIDI_NEW_CHANNEL_ASYNC_CALL"
        self._verifyScriptSucceeds(_CLIENT_FORK_SCRIPT_TEMPLATE % (test_case, self._port))

    def testInProgressBidiNewChannelBlockingCall(self):
        test_case = "IN_PROGRESS_BIDI_NEW_CHANNEL_BLOCKING_CALL"
        self._verifyScriptSucceeds(_CLIENT_FORK_SCRIPT_TEMPLATE % (test_case, self._port))

    def tearDown(self):
        self._process.kill()


    def _verifyScriptSucceeds(self, script):
        process = subprocess.Popen(
            [sys.executable, '-c', script],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE)
        out, err = process.communicate()
        self.assertEqual(
            0, process.returncode,
            'process failed with exit code %d (stdout: %s, stderr: %s)' %
            (process.returncode, out, err))
        return out, err

if __name__ == '__main__':
    unittest.main(verbosity=2)
