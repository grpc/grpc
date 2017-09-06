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
"""Common code for unit tests of the interoperability test code."""

from tests.interop import methods


class IntraopTestCase(object):
    """Unit test methods.

  This class must be mixed in with unittest.TestCase and a class that defines
  setUp and tearDown methods that manage a stub attribute.
  """

    def testEmptyUnary(self):
        methods.TestCase.EMPTY_UNARY.test_interoperability(self.stub, None)

    def testLargeUnary(self):
        methods.TestCase.LARGE_UNARY.test_interoperability(self.stub, None)

    def testServerStreaming(self):
        methods.TestCase.SERVER_STREAMING.test_interoperability(self.stub, None)

    def testClientStreaming(self):
        methods.TestCase.CLIENT_STREAMING.test_interoperability(self.stub, None)

    def testPingPong(self):
        methods.TestCase.PING_PONG.test_interoperability(self.stub, None)

    def testCancelAfterBegin(self):
        methods.TestCase.CANCEL_AFTER_BEGIN.test_interoperability(self.stub,
                                                                  None)

    def testCancelAfterFirstResponse(self):
        methods.TestCase.CANCEL_AFTER_FIRST_RESPONSE.test_interoperability(
            self.stub, None)

    def testTimeoutOnSleepingServer(self):
        methods.TestCase.TIMEOUT_ON_SLEEPING_SERVER.test_interoperability(
            self.stub, None)
