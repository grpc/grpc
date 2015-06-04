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

"""Test of the GRPC-backed ForeLink and RearLink."""

import threading
import unittest

from grpc._adapter import _proto_scenarios
from grpc._adapter import _test_links
from grpc._adapter import fore
from grpc._adapter import rear
from grpc.framework.base import interfaces
from grpc.framework.foundation import logging_pool

_IDENTITY = lambda x: x
_TIMEOUT = 32


# TODO(nathaniel): End-to-end metadata testing.
def _transform_metadata(unused_metadata):
  return (
      ('one unused key', 'one unused value'),
      ('another unused key', 'another unused value'),
)


class RoundTripTest(unittest.TestCase):

  def setUp(self):
    self.fore_link_pool = logging_pool.pool(8)
    self.rear_link_pool = logging_pool.pool(8)

  def tearDown(self):
    self.rear_link_pool.shutdown(wait=True)
    self.fore_link_pool.shutdown(wait=True)

  def testZeroMessageRoundTrip(self):
    test_operation_id = object()
    test_method = 'test method'
    test_fore_link = _test_links.ForeLink(None, None)
    def rear_action(front_to_back_ticket, fore_link):
      if front_to_back_ticket.kind in (
          interfaces.FrontToBackTicket.Kind.COMPLETION,
          interfaces.FrontToBackTicket.Kind.ENTIRE):
        back_to_front_ticket = interfaces.BackToFrontTicket(
            front_to_back_ticket.operation_id, 0,
            interfaces.BackToFrontTicket.Kind.COMPLETION, None)
        fore_link.accept_back_to_front_ticket(back_to_front_ticket)
    test_rear_link = _test_links.RearLink(rear_action, None)

    fore_link = fore.ForeLink(
        self.fore_link_pool, {test_method: None}, {test_method: None}, None, ())
    fore_link.join_rear_link(test_rear_link)
    test_rear_link.join_fore_link(fore_link)
    fore_link.start()
    port = fore_link.port()

    rear_link = rear.RearLink(
        'localhost', port, self.rear_link_pool, {test_method: None},
        {test_method: None}, False, None, None, None,
        metadata_transformer=_transform_metadata)
    rear_link.join_fore_link(test_fore_link)
    test_fore_link.join_rear_link(rear_link)
    rear_link.start()

    front_to_back_ticket = interfaces.FrontToBackTicket(
        test_operation_id, 0, interfaces.FrontToBackTicket.Kind.ENTIRE,
        test_method, interfaces.ServicedSubscription.Kind.FULL, None, None,
        _TIMEOUT)
    rear_link.accept_front_to_back_ticket(front_to_back_ticket)

    with test_fore_link.condition:
      while (not test_fore_link.tickets or
             test_fore_link.tickets[-1].kind is
                 interfaces.BackToFrontTicket.Kind.CONTINUATION):
        test_fore_link.condition.wait()

    rear_link.stop()
    fore_link.stop()

    with test_fore_link.condition:
      self.assertIs(
          test_fore_link.tickets[-1].kind,
          interfaces.BackToFrontTicket.Kind.COMPLETION)

  def testEntireRoundTrip(self):
    test_operation_id = object()
    test_method = 'test method'
    test_front_to_back_datum = b'\x07'
    test_back_to_front_datum = b'\x08'
    test_fore_link = _test_links.ForeLink(None, None)
    rear_sequence_number = [0]
    def rear_action(front_to_back_ticket, fore_link):
      if front_to_back_ticket.payload is None:
        payload = None
      else:
        payload = test_back_to_front_datum
      terminal = front_to_back_ticket.kind in (
          interfaces.FrontToBackTicket.Kind.COMPLETION,
          interfaces.FrontToBackTicket.Kind.ENTIRE)
      if payload is not None or terminal:
        if terminal:
          kind = interfaces.BackToFrontTicket.Kind.COMPLETION
        else:
          kind = interfaces.BackToFrontTicket.Kind.CONTINUATION
        back_to_front_ticket = interfaces.BackToFrontTicket(
            front_to_back_ticket.operation_id, rear_sequence_number[0], kind,
            payload)
        rear_sequence_number[0] += 1
        fore_link.accept_back_to_front_ticket(back_to_front_ticket)
    test_rear_link = _test_links.RearLink(rear_action, None)

    fore_link = fore.ForeLink(
        self.fore_link_pool, {test_method: _IDENTITY},
        {test_method: _IDENTITY}, None, ())
    fore_link.join_rear_link(test_rear_link)
    test_rear_link.join_fore_link(fore_link)
    fore_link.start()
    port = fore_link.port()

    rear_link = rear.RearLink(
        'localhost', port, self.rear_link_pool, {test_method: _IDENTITY},
        {test_method: _IDENTITY}, False, None, None, None)
    rear_link.join_fore_link(test_fore_link)
    test_fore_link.join_rear_link(rear_link)
    rear_link.start()

    front_to_back_ticket = interfaces.FrontToBackTicket(
        test_operation_id, 0, interfaces.FrontToBackTicket.Kind.ENTIRE,
        test_method, interfaces.ServicedSubscription.Kind.FULL, None,
        test_front_to_back_datum, _TIMEOUT)
    rear_link.accept_front_to_back_ticket(front_to_back_ticket)

    with test_fore_link.condition:
      while (not test_fore_link.tickets or
             test_fore_link.tickets[-1].kind is not
                 interfaces.BackToFrontTicket.Kind.COMPLETION):
        test_fore_link.condition.wait()

    rear_link.stop()
    fore_link.stop()

    with test_rear_link.condition:
      front_to_back_payloads = tuple(
          ticket.payload for ticket in test_rear_link.tickets
          if ticket.payload is not None)
    with test_fore_link.condition:
      back_to_front_payloads = tuple(
          ticket.payload for ticket in test_fore_link.tickets
          if ticket.payload is not None)
    self.assertTupleEqual((test_front_to_back_datum,), front_to_back_payloads)
    self.assertTupleEqual((test_back_to_front_datum,), back_to_front_payloads)

  def _perform_scenario_test(self, scenario):
    test_operation_id = object()
    test_method = scenario.method()
    test_fore_link = _test_links.ForeLink(None, None)
    rear_lock = threading.Lock()
    rear_sequence_number = [0]
    def rear_action(front_to_back_ticket, fore_link):
      with rear_lock:
        if front_to_back_ticket.payload is not None:
          response = scenario.response_for_request(front_to_back_ticket.payload)
        else:
          response = None
      terminal = front_to_back_ticket.kind in (
          interfaces.FrontToBackTicket.Kind.COMPLETION,
          interfaces.FrontToBackTicket.Kind.ENTIRE)
      if response is not None or terminal:
        if terminal:
          kind = interfaces.BackToFrontTicket.Kind.COMPLETION
        else:
          kind = interfaces.BackToFrontTicket.Kind.CONTINUATION
        back_to_front_ticket = interfaces.BackToFrontTicket(
            front_to_back_ticket.operation_id, rear_sequence_number[0], kind,
            response)
        rear_sequence_number[0] += 1
        fore_link.accept_back_to_front_ticket(back_to_front_ticket)
    test_rear_link = _test_links.RearLink(rear_action, None)

    fore_link = fore.ForeLink(
        self.fore_link_pool, {test_method: scenario.deserialize_request},
        {test_method: scenario.serialize_response}, None, ())
    fore_link.join_rear_link(test_rear_link)
    test_rear_link.join_fore_link(fore_link)
    fore_link.start()
    port = fore_link.port()

    rear_link = rear.RearLink(
        'localhost', port, self.rear_link_pool,
        {test_method: scenario.serialize_request},
        {test_method: scenario.deserialize_response}, False, None, None, None)
    rear_link.join_fore_link(test_fore_link)
    test_fore_link.join_rear_link(rear_link)
    rear_link.start()

    commencement_ticket = interfaces.FrontToBackTicket(
        test_operation_id, 0,
        interfaces.FrontToBackTicket.Kind.COMMENCEMENT, test_method,
        interfaces.ServicedSubscription.Kind.FULL, None, None,
        _TIMEOUT)
    fore_sequence_number = 1
    rear_link.accept_front_to_back_ticket(commencement_ticket)
    for request in scenario.requests():
      continuation_ticket = interfaces.FrontToBackTicket(
          test_operation_id, fore_sequence_number,
          interfaces.FrontToBackTicket.Kind.CONTINUATION, None, None, None,
          request, None)
      fore_sequence_number += 1
      rear_link.accept_front_to_back_ticket(continuation_ticket)
    completion_ticket = interfaces.FrontToBackTicket(
        test_operation_id, fore_sequence_number,
        interfaces.FrontToBackTicket.Kind.COMPLETION, None, None, None, None,
        None)
    fore_sequence_number += 1
    rear_link.accept_front_to_back_ticket(completion_ticket)

    with test_fore_link.condition:
      while (not test_fore_link.tickets or
             test_fore_link.tickets[-1].kind is not
                 interfaces.BackToFrontTicket.Kind.COMPLETION):
        test_fore_link.condition.wait()

    rear_link.stop()
    fore_link.stop()

    with test_rear_link.condition:
      requests = tuple(
          ticket.payload for ticket in test_rear_link.tickets
          if ticket.payload is not None)
    with test_fore_link.condition:
      responses = tuple(
          ticket.payload for ticket in test_fore_link.tickets
          if ticket.payload is not None)
    self.assertTrue(scenario.verify_requests(requests))
    self.assertTrue(scenario.verify_responses(responses))

  def testEmptyScenario(self):
    self._perform_scenario_test(_proto_scenarios.EmptyScenario())

  def testBidirectionallyUnaryScenario(self):
    self._perform_scenario_test(_proto_scenarios.BidirectionallyUnaryScenario())

  def testBidirectionallyStreamingScenario(self):
    self._perform_scenario_test(
        _proto_scenarios.BidirectionallyStreamingScenario())


if __name__ == '__main__':
  unittest.main(verbosity=2)
