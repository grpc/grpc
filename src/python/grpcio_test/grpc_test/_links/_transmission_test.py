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

"""Tests transmission of tickets across gRPC-on-the-wire."""

import unittest

from grpc._adapter import _intermediary_low
from grpc._links import invocation
from grpc._links import service
from grpc.framework.interfaces.links import links
from grpc_test._links import _proto_scenarios
from grpc_test.framework.common import test_constants
from grpc_test.framework.interfaces.links import test_cases
from grpc_test.framework.interfaces.links import test_utilities

_IDENTITY = lambda x: x


class TransmissionTest(test_cases.TransmissionTest, unittest.TestCase):

  def create_transmitting_links(self):
    service_link = service.service_link(
        {self.group_and_method(): self.deserialize_request},
        {self.group_and_method(): self.serialize_response})
    port = service_link.add_port(0, None)
    service_link.start()
    channel = _intermediary_low.Channel('localhost:%d' % port, None)
    invocation_link = invocation.invocation_link(
        channel, 'localhost',
        {self.group_and_method(): self.serialize_request},
        {self.group_and_method(): self.deserialize_response})
    invocation_link.start()
    return invocation_link, service_link

  def destroy_transmitting_links(self, invocation_side_link, service_side_link):
    invocation_side_link.stop()
    service_side_link.stop_gracefully()

  def create_invocation_initial_metadata(self):
    return (
        ('first invocation initial metadata key', 'just a string value'),
        ('second invocation initial metadata key', '0123456789'),
        ('third invocation initial metadata key-bin', '\x00\x57' * 100),
    )

  def create_invocation_terminal_metadata(self):
    return None

  def create_service_initial_metadata(self):
    return (
        ('first service initial metadata key', 'just another string value'),
        ('second service initial metadata key', '9876543210'),
        ('third service initial metadata key-bin', '\x00\x59\x02' * 100),
    )

  def create_service_terminal_metadata(self):
    return (
        ('first service terminal metadata key', 'yet another string value'),
        ('second service terminal metadata key', 'abcdefghij'),
        ('third service terminal metadata key-bin', '\x00\x37' * 100),
    )

  def create_invocation_completion(self):
    return None, None

  def create_service_completion(self):
    return _intermediary_low.Code.OK, 'An exuberant test "details" message!'

  def assertMetadataTransmitted(self, original_metadata, transmitted_metadata):
    # we need to filter out any additional metadata added in transmitted_metadata
    # since implementations are allowed to add to what is sent (in any position)
    keys, _ = zip(*original_metadata)
    self.assertSequenceEqual(
        original_metadata,
        [x for x in transmitted_metadata if x[0] in keys])


class RoundTripTest(unittest.TestCase):

  def testZeroMessageRoundTrip(self):
    test_operation_id = object()
    test_group = 'test package.Test Group'
    test_method = 'test method'
    identity_transformation = {(test_group, test_method): _IDENTITY}
    test_code = _intermediary_low.Code.OK
    test_message = 'a test message'

    service_link = service.service_link(
        identity_transformation, identity_transformation)
    service_mate = test_utilities.RecordingLink()
    service_link.join_link(service_mate)
    port = service_link.add_port(0, None)
    service_link.start()
    channel = _intermediary_low.Channel('localhost:%d' % port, None)
    invocation_link = invocation.invocation_link(
        channel, 'localhost', identity_transformation, identity_transformation)
    invocation_mate = test_utilities.RecordingLink()
    invocation_link.join_link(invocation_mate)
    invocation_link.start()

    invocation_ticket = links.Ticket(
        test_operation_id, 0, test_group, test_method,
        links.Ticket.Subscription.FULL, test_constants.LONG_TIMEOUT, None, None,
        None, None, None, None, links.Ticket.Termination.COMPLETION)
    invocation_link.accept_ticket(invocation_ticket)
    service_mate.block_until_tickets_satisfy(test_cases.terminated)

    service_ticket = links.Ticket(
        service_mate.tickets()[-1].operation_id, 0, None, None, None, None,
        None, None, None, None, test_code, test_message,
        links.Ticket.Termination.COMPLETION)
    service_link.accept_ticket(service_ticket)
    invocation_mate.block_until_tickets_satisfy(test_cases.terminated)

    invocation_link.stop()
    service_link.stop_gracefully()

    self.assertIs(
        service_mate.tickets()[-1].termination,
        links.Ticket.Termination.COMPLETION)
    self.assertIs(
        invocation_mate.tickets()[-1].termination,
        links.Ticket.Termination.COMPLETION)

  def _perform_scenario_test(self, scenario):
    test_operation_id = object()
    test_group, test_method = scenario.group_and_method()
    test_code = _intermediary_low.Code.OK
    test_message = 'a scenario test message'

    service_link = service.service_link(
        {(test_group, test_method): scenario.deserialize_request},
        {(test_group, test_method): scenario.serialize_response})
    service_mate = test_utilities.RecordingLink()
    service_link.join_link(service_mate)
    port = service_link.add_port(0, None)
    service_link.start()
    channel = _intermediary_low.Channel('localhost:%d' % port, None)
    invocation_link = invocation.invocation_link(
        channel, 'localhost',
        {(test_group, test_method): scenario.serialize_request},
        {(test_group, test_method): scenario.deserialize_response})
    invocation_mate = test_utilities.RecordingLink()
    invocation_link.join_link(invocation_mate)
    invocation_link.start()

    invocation_ticket = links.Ticket(
        test_operation_id, 0, test_group, test_method,
        links.Ticket.Subscription.FULL, test_constants.LONG_TIMEOUT, None, None,
        None, None, None, None, None)
    invocation_link.accept_ticket(invocation_ticket)
    requests = scenario.requests()
    for request_index, request in enumerate(requests):
      request_ticket = links.Ticket(
          test_operation_id, 1 + request_index, None, None, None, None, 1, None,
          request, None, None, None, None)
      invocation_link.accept_ticket(request_ticket)
      service_mate.block_until_tickets_satisfy(
          test_cases.at_least_n_payloads_received_predicate(1 + request_index))
      response_ticket = links.Ticket(
          service_mate.tickets()[0].operation_id, request_index, None, None,
          None, None, 1, None, scenario.response_for_request(request), None,
          None, None, None)
      service_link.accept_ticket(response_ticket)
      invocation_mate.block_until_tickets_satisfy(
          test_cases.at_least_n_payloads_received_predicate(1 + request_index))
    request_count = len(requests)
    invocation_completion_ticket = links.Ticket(
        test_operation_id, request_count + 1, None, None, None, None, None,
        None, None, None, None, None, links.Ticket.Termination.COMPLETION)
    invocation_link.accept_ticket(invocation_completion_ticket)
    service_mate.block_until_tickets_satisfy(test_cases.terminated)
    service_completion_ticket = links.Ticket(
        service_mate.tickets()[0].operation_id, request_count, None, None, None,
        None, None, None, None, None, test_code, test_message,
        links.Ticket.Termination.COMPLETION)
    service_link.accept_ticket(service_completion_ticket)
    invocation_mate.block_until_tickets_satisfy(test_cases.terminated)

    invocation_link.stop()
    service_link.stop_gracefully()

    observed_requests = tuple(
        ticket.payload for ticket in service_mate.tickets()
        if ticket.payload is not None)
    observed_responses = tuple(
        ticket.payload for ticket in invocation_mate.tickets()
        if ticket.payload is not None)
    self.assertTrue(scenario.verify_requests(observed_requests))
    self.assertTrue(scenario.verify_responses(observed_responses))

  def testEmptyScenario(self):
    self._perform_scenario_test(_proto_scenarios.EmptyScenario())

  def testBidirectionallyUnaryScenario(self):
    self._perform_scenario_test(_proto_scenarios.BidirectionallyUnaryScenario())

  def testBidirectionallyStreamingScenario(self):
    self._perform_scenario_test(
        _proto_scenarios.BidirectionallyStreamingScenario())


if __name__ == '__main__':
  unittest.main(verbosity=2)
