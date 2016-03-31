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

"""Tests of the links interface of RPC Framework."""

# unittest is referenced from specification in this module.
import abc
import unittest  # pylint: disable=unused-import

import six

from grpc.framework.interfaces.links import links
from tests.unit.framework.common import test_constants
from tests.unit.framework.interfaces.links import test_utilities


def at_least_n_payloads_received_predicate(n):
  def predicate(ticket_sequence):
    payload_count = 0
    for ticket in ticket_sequence:
      if ticket.payload is not None:
        payload_count += 1
        if n <= payload_count:
          return True
    else:
      return False
  return predicate


def terminated(ticket_sequence):
  return ticket_sequence and ticket_sequence[-1].termination is not None

_TRANSMISSION_GROUP = 'test.Group'
_TRANSMISSION_METHOD = 'TestMethod'


class TransmissionTest(six.with_metaclass(abc.ABCMeta)):
  """Tests ticket transmission between two connected links.

  This class must be mixed into a unittest.TestCase that implements the abstract
  methods it provides.
  """

  # This is a unittest.TestCase mix-in.
  # pylint: disable=invalid-name

  @abc.abstractmethod
  def create_transmitting_links(self):
    """Creates two connected links for use in this test.

    Returns:
      Two links.Links, the first of which will be used on the invocation side
        of RPCs and the second of which will be used on the service side of
        RPCs.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def destroy_transmitting_links(self, invocation_side_link, service_side_link):
    """Destroys the two connected links created for this test.


    Args:
      invocation_side_link: The link used on the invocation side of RPCs in
        this test.
      service_side_link: The link used on the service side of RPCs in this
        test.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def create_invocation_initial_metadata(self):
    """Creates a value for use as invocation-side initial metadata.

    Returns:
      A metadata value appropriate for use as invocation-side initial metadata
        or None if invocation-side initial metadata transmission is not
        supported by the links under test.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def create_invocation_terminal_metadata(self):
    """Creates a value for use as invocation-side terminal metadata.

    Returns:
      A metadata value appropriate for use as invocation-side terminal
        metadata or None if invocation-side terminal metadata transmission is
        not supported by the links under test.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def create_service_initial_metadata(self):
    """Creates a value for use as service-side initial metadata.

    Returns:
      A metadata value appropriate for use as service-side initial metadata or
        None if service-side initial metadata transmission is not supported by
        the links under test.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def create_service_terminal_metadata(self):
    """Creates a value for use as service-side terminal metadata.

    Returns:
      A metadata value appropriate for use as service-side terminal metadata or
        None if service-side terminal metadata transmission is not supported by
        the links under test.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def create_invocation_completion(self):
    """Creates values for use as invocation-side code and message.

    Returns:
      An invocation-side code value and an invocation-side message value.
        Either or both may be None if invocation-side code and/or
        invocation-side message transmission is not supported by the links
        under test.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def create_service_completion(self):
    """Creates values for use as service-side code and message.

    Returns:
      A service-side code value and a service-side message value. Either or
        both may be None if service-side code and/or service-side message
        transmission is not supported by the links under test.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def assertMetadataTransmitted(self, original_metadata, transmitted_metadata):
    """Asserts that transmitted_metadata contains original_metadata.

    Args:
      original_metadata: A metadata object used in this test.
      transmitted_metadata: A metadata object obtained after transmission
        through the system under test.

    Raises:
      AssertionError: if the transmitted_metadata object does not contain
        original_metadata.
    """
    raise NotImplementedError()

  def group_and_method(self):
    """Returns the group and method used in this test case.

    Returns:
      A pair of the group and method used in this test case.
    """
    return _TRANSMISSION_GROUP, _TRANSMISSION_METHOD

  def serialize_request(self, request):
    """Serializes a request value used in this test case.

    Args:
      request: A request value created by this test case.

    Returns:
      A bytestring that is the serialization of the given request.
    """
    return request

  def deserialize_request(self, serialized_request):
    """Deserializes a request value used in this test case.

    Args:
      serialized_request: A bytestring that is the serialization of some request
        used in this test case.

    Returns:
      The request value encoded by the given bytestring.
    """
    return serialized_request

  def serialize_response(self, response):
    """Serializes a response value used in this test case.

    Args:
      response: A response value created by this test case.

    Returns:
      A bytestring that is the serialization of the given response.
    """
    return response

  def deserialize_response(self, serialized_response):
    """Deserializes a response value used in this test case.

    Args:
      serialized_response: A bytestring that is the serialization of some
        response used in this test case.

    Returns:
      The response value encoded by the given bytestring.
    """
    return serialized_response

  def _assert_is_valid_metadata_payload_sequence(
      self, ticket_sequence, payloads, initial_metadata, terminal_metadata):
    initial_metadata_seen = False
    seen_payloads = []
    terminal_metadata_seen = False

    for ticket in ticket_sequence:
      if ticket.initial_metadata is not None:
        self.assertFalse(initial_metadata_seen)
        self.assertFalse(seen_payloads)
        self.assertFalse(terminal_metadata_seen)
        self.assertMetadataTransmitted(initial_metadata, ticket.initial_metadata)
        initial_metadata_seen = True

      if ticket.payload is not None:
        self.assertFalse(terminal_metadata_seen)
        seen_payloads.append(ticket.payload)

      if ticket.terminal_metadata is not None:
        self.assertFalse(terminal_metadata_seen)
        self.assertMetadataTransmitted(terminal_metadata, ticket.terminal_metadata)
        terminal_metadata_seen = True
    self.assertSequenceEqual(payloads, seen_payloads)

  def _assert_is_valid_invocation_sequence(
      self, ticket_sequence, group, method, payloads, initial_metadata,
      terminal_metadata, termination):
    self.assertLess(0, len(ticket_sequence))
    self.assertEqual(group, ticket_sequence[0].group)
    self.assertEqual(method, ticket_sequence[0].method)
    self._assert_is_valid_metadata_payload_sequence(
        ticket_sequence, payloads, initial_metadata, terminal_metadata)
    self.assertIs(termination, ticket_sequence[-1].termination)

  def _assert_is_valid_service_sequence(
      self, ticket_sequence, payloads, initial_metadata, terminal_metadata,
      code, message, termination):
    self.assertLess(0, len(ticket_sequence))
    self._assert_is_valid_metadata_payload_sequence(
        ticket_sequence, payloads, initial_metadata, terminal_metadata)
    self.assertEqual(code, ticket_sequence[-1].code)
    self.assertEqual(message, ticket_sequence[-1].message)
    self.assertIs(termination, ticket_sequence[-1].termination)

  def setUp(self):
    self._invocation_link, self._service_link = self.create_transmitting_links()
    self._invocation_mate = test_utilities.RecordingLink()
    self._service_mate = test_utilities.RecordingLink()
    self._invocation_link.join_link(self._invocation_mate)
    self._service_link.join_link(self._service_mate)

  def tearDown(self):
    self.destroy_transmitting_links(self._invocation_link, self._service_link)

  def testSimplestRoundTrip(self):
    """Tests transmission of one ticket in each direction."""
    invocation_operation_id = object()
    invocation_payload = b'\x07' * 1023
    timeout = test_constants.LONG_TIMEOUT
    invocation_initial_metadata = self.create_invocation_initial_metadata()
    invocation_terminal_metadata = self.create_invocation_terminal_metadata()
    invocation_code, invocation_message = self.create_invocation_completion()
    service_payload = b'\x08' * 1025
    service_initial_metadata = self.create_service_initial_metadata()
    service_terminal_metadata = self.create_service_terminal_metadata()
    service_code, service_message = self.create_service_completion()

    original_invocation_ticket = links.Ticket(
        invocation_operation_id, 0, _TRANSMISSION_GROUP, _TRANSMISSION_METHOD,
        links.Ticket.Subscription.FULL, timeout, 0, invocation_initial_metadata,
        invocation_payload, invocation_terminal_metadata, invocation_code,
        invocation_message, links.Ticket.Termination.COMPLETION, None)
    self._invocation_link.accept_ticket(original_invocation_ticket)

    self._service_mate.block_until_tickets_satisfy(
        at_least_n_payloads_received_predicate(1))
    service_operation_id = self._service_mate.tickets()[0].operation_id

    self._service_mate.block_until_tickets_satisfy(terminated)
    self._assert_is_valid_invocation_sequence(
        self._service_mate.tickets(), _TRANSMISSION_GROUP, _TRANSMISSION_METHOD,
        (invocation_payload,), invocation_initial_metadata,
        invocation_terminal_metadata, links.Ticket.Termination.COMPLETION)

    original_service_ticket = links.Ticket(
        service_operation_id, 0, None, None, links.Ticket.Subscription.FULL,
        timeout, 0, service_initial_metadata, service_payload,
        service_terminal_metadata, service_code, service_message,
        links.Ticket.Termination.COMPLETION, None)
    self._service_link.accept_ticket(original_service_ticket)
    self._invocation_mate.block_until_tickets_satisfy(terminated)
    self._assert_is_valid_service_sequence(
        self._invocation_mate.tickets(), (service_payload,),
        service_initial_metadata, service_terminal_metadata, service_code,
        service_message, links.Ticket.Termination.COMPLETION)
