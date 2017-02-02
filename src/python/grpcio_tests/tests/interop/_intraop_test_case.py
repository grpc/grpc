# Copyright 2015, Google Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
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
