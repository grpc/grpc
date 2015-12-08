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

"""A test of invocation-side code unconnected to an RPC server."""

import unittest

from grpc._adapter import _intermediary_low
from grpc._links import invocation
from grpc.framework.interfaces.links import links
from tests.unit.framework.common import test_constants
from tests.unit.framework.interfaces.links import test_cases
from tests.unit.framework.interfaces.links import test_utilities

_NULL_BEHAVIOR = lambda unused_argument: None


class LonelyInvocationLinkTest(unittest.TestCase):

  def testUpAndDown(self):
    channel = _intermediary_low.Channel('nonexistent:54321', None)
    invocation_link = invocation.invocation_link(
        channel, 'nonexistent', None, {}, {})

    invocation_link.start()
    invocation_link.stop()

  def _test_lonely_invocation_with_termination(self, termination):
    test_operation_id = object()
    test_group = 'test package.Test Service'
    test_method = 'test method'
    invocation_link_mate = test_utilities.RecordingLink()

    channel = _intermediary_low.Channel('nonexistent:54321', None)
    invocation_link = invocation.invocation_link(
        channel, 'nonexistent', None, {}, {})
    invocation_link.join_link(invocation_link_mate)
    invocation_link.start()

    ticket = links.Ticket(
        test_operation_id, 0, test_group, test_method,
        links.Ticket.Subscription.FULL, test_constants.SHORT_TIMEOUT, 1, None,
        None, None, None, None, termination, None)
    invocation_link.accept_ticket(ticket)
    invocation_link_mate.block_until_tickets_satisfy(test_cases.terminated)

    invocation_link.stop()

    self.assertIsNot(
        invocation_link_mate.tickets()[-1].termination,
        links.Ticket.Termination.COMPLETION)

  def testLonelyInvocationLinkWithCommencementTicket(self):
    self._test_lonely_invocation_with_termination(None)

  def testLonelyInvocationLinkWithEntireTicket(self):
    self._test_lonely_invocation_with_termination(
        links.Ticket.Termination.COMPLETION)


if __name__ == '__main__':
  unittest.main()
