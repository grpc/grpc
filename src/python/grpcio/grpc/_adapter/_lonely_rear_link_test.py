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

from grpc._adapter import _test_links
from grpc._adapter import rear
from grpc.framework.base import interfaces
from grpc.framework.foundation import logging_pool

_IDENTITY = lambda x: x
_TIMEOUT = 2


class LonelyRearLinkTest(unittest.TestCase):

  def setUp(self):
    self.pool = logging_pool.pool(8)

  def tearDown(self):
    self.pool.shutdown(wait=True)

  def testUpAndDown(self):
    rear_link = rear.RearLink(
        'nonexistent', 54321, self.pool, {}, {}, False, None, None, None)

    rear_link.start()
    rear_link.stop()

  def _perform_lonely_client_test_with_ticket_kind(
      self, front_to_back_ticket_kind):
    test_operation_id = object()
    test_method = 'test method'
    fore_link = _test_links.ForeLink(None, None)

    rear_link = rear.RearLink(
        'nonexistent', 54321, self.pool, {test_method: None},
        {test_method: None}, False, None, None, None)
    rear_link.join_fore_link(fore_link)
    rear_link.start()

    front_to_back_ticket = interfaces.FrontToBackTicket(
        test_operation_id, 0, front_to_back_ticket_kind, test_method,
        interfaces.ServicedSubscription.Kind.FULL, None, None, _TIMEOUT)
    rear_link.accept_front_to_back_ticket(front_to_back_ticket)

    with fore_link.condition:
      while True:
        if (fore_link.tickets and
            fore_link.tickets[-1].kind is not
                interfaces.BackToFrontTicket.Kind.CONTINUATION):
          break
        fore_link.condition.wait()

    rear_link.stop()

    with fore_link.condition:
      self.assertIsNot(
          fore_link.tickets[-1].kind,
          interfaces.BackToFrontTicket.Kind.COMPLETION)

  def testLonelyClientCommencementTicket(self):
    self._perform_lonely_client_test_with_ticket_kind(
        interfaces.FrontToBackTicket.Kind.COMMENCEMENT)

  def testLonelyClientEntireTicket(self):
    self._perform_lonely_client_test_with_ticket_kind(
        interfaces.FrontToBackTicket.Kind.ENTIRE)


if __name__ == '__main__':
  unittest.main(verbosity=2)
