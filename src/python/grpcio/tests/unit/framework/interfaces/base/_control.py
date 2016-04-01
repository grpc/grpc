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

"""Part of the tests of the base interface of RPC Framework."""

from __future__ import division

import abc
import collections
import enum
import random  # pylint: disable=unused-import
import threading
import time

import six

from grpc.framework.interfaces.base import base
from tests.unit.framework.common import test_constants
from tests.unit.framework.interfaces.base import _sequence
from tests.unit.framework.interfaces.base import _state
from tests.unit.framework.interfaces.base import test_interfaces  # pylint: disable=unused-import

_GROUP = 'base test cases test group'
_METHOD = 'base test cases test method'

_PAYLOAD_RANDOM_SECTION_MAXIMUM_SIZE = test_constants.PAYLOAD_SIZE // 20
_MINIMUM_PAYLOAD_SIZE = test_constants.PAYLOAD_SIZE // 600


def _create_payload(randomness):
  length = randomness.randint(
      _MINIMUM_PAYLOAD_SIZE, test_constants.PAYLOAD_SIZE)
  random_section_length = randomness.randint(
      0, min(_PAYLOAD_RANDOM_SECTION_MAXIMUM_SIZE, length))
  random_section = bytes(
      bytearray(
          randomness.getrandbits(8) for _ in range(random_section_length)))
  sevens_section = b'\x07' * (length - random_section_length)
  return b''.join(randomness.sample((random_section, sevens_section), 2))


def _anything_in_flight(state):
  return (
      state.invocation_initial_metadata_in_flight is not None or
      state.invocation_payloads_in_flight or
      state.invocation_completion_in_flight is not None or
      state.service_initial_metadata_in_flight is not None or
      state.service_payloads_in_flight or
      state.service_completion_in_flight is not None or
      0 < state.invocation_allowance_in_flight or
      0 < state.service_allowance_in_flight
  )


def _verify_service_advance_and_update_state(
    initial_metadata, payload, completion, allowance, state, implementation):
  if initial_metadata is not None:
    if state.invocation_initial_metadata_received:
      return 'Later invocation initial metadata received: %s' % (
          initial_metadata,)
    if state.invocation_payloads_received:
      return 'Invocation initial metadata received after payloads: %s' % (
          state.invocation_payloads_received)
    if state.invocation_completion_received:
      return 'Invocation initial metadata received after invocation completion!'
    if not implementation.metadata_transmitted(
        state.invocation_initial_metadata_in_flight, initial_metadata):
      return 'Invocation initial metadata maltransmitted: %s, %s' % (
          state.invocation_initial_metadata_in_flight, initial_metadata)
    else:
      state.invocation_initial_metadata_in_flight = None
      state.invocation_initial_metadata_received = True

  if payload is not None:
    if state.invocation_completion_received:
      return 'Invocation payload received after invocation completion!'
    elif not state.invocation_payloads_in_flight:
      return 'Invocation payload "%s" received but not in flight!' % (payload,)
    elif state.invocation_payloads_in_flight[0] != payload:
      return 'Invocation payload mismatch: %s, %s' % (
          state.invocation_payloads_in_flight[0], payload)
    elif state.service_side_invocation_allowance < 1:
      return 'Disallowed invocation payload!'
    else:
      state.invocation_payloads_in_flight.pop(0)
      state.invocation_payloads_received += 1
      state.service_side_invocation_allowance -= 1

  if completion is not None:
    if state.invocation_completion_received:
      return 'Later invocation completion received: %s' % (completion,)
    elif not implementation.completion_transmitted(
        state.invocation_completion_in_flight, completion):
      return 'Invocation completion maltransmitted: %s, %s' % (
          state.invocation_completion_in_flight, completion)
    else:
      state.invocation_completion_in_flight = None
      state.invocation_completion_received = True

  if allowance is not None:
    if allowance <= 0:
      return 'Illegal allowance value: %s' % (allowance,)
    else:
      state.service_allowance_in_flight -= allowance
      state.service_side_service_allowance += allowance


def _verify_invocation_advance_and_update_state(
    initial_metadata, payload, completion, allowance, state, implementation):
  if initial_metadata is not None:
    if state.service_initial_metadata_received:
      return 'Later service initial metadata received: %s' % (initial_metadata,)
    if state.service_payloads_received:
      return 'Service initial metadata received after service payloads: %s' % (
          state.service_payloads_received)
    if state.service_completion_received:
      return 'Service initial metadata received after service completion!'
    if not implementation.metadata_transmitted(
        state.service_initial_metadata_in_flight, initial_metadata):
      return 'Service initial metadata maltransmitted: %s, %s' % (
          state.service_initial_metadata_in_flight, initial_metadata)
    else:
      state.service_initial_metadata_in_flight = None
      state.service_initial_metadata_received = True

  if payload is not None:
    if state.service_completion_received:
      return 'Service payload received after service completion!'
    elif not state.service_payloads_in_flight:
      return 'Service payload "%s" received but not in flight!' % (payload,)
    elif state.service_payloads_in_flight[0] != payload:
      return 'Service payload mismatch: %s, %s' % (
          state.invocation_payloads_in_flight[0], payload)
    elif state.invocation_side_service_allowance < 1:
      return 'Disallowed service payload!'
    else:
      state.service_payloads_in_flight.pop(0)
      state.service_payloads_received += 1
      state.invocation_side_service_allowance -= 1

  if completion is not None:
    if state.service_completion_received:
      return 'Later service completion received: %s' % (completion,)
    elif not implementation.completion_transmitted(
        state.service_completion_in_flight, completion):
      return 'Service completion maltransmitted: %s, %s' % (
          state.service_completion_in_flight, completion)
    else:
      state.service_completion_in_flight = None
      state.service_completion_received = True

  if allowance is not None:
    if allowance <= 0:
      return 'Illegal allowance value: %s' % (allowance,)
    else:
      state.invocation_allowance_in_flight -= allowance
      state.invocation_side_service_allowance += allowance


class Invocation(
    collections.namedtuple(
        'Invocation',
        ('group', 'method', 'subscription_kind', 'timeout', 'initial_metadata',
         'payload', 'completion',))):
  """A description of operation invocation.

  Attributes:
    group: The group identifier for the operation.
    method: The method identifier for the operation.
    subscription_kind: A base.Subscription.Kind value describing the kind of
      subscription to use for the operation.
    timeout: A duration in seconds to pass as the timeout value for the
      operation.
    initial_metadata: An object to pass as the initial metadata for the
      operation or None.
    payload: An object to pass as a payload value for the operation or None.
    completion: An object to pass as a completion value for the operation or
      None.
  """


class OnAdvance(
    collections.namedtuple(
        'OnAdvance',
        ('kind', 'initial_metadata', 'payload', 'completion', 'allowance'))):
  """Describes action to be taken in a test in response to an advance call.

  Attributes:
    kind: A Kind value describing the overall kind of response.
    initial_metadata: An initial metadata value to pass to a call of the advance
      method of the operator under test. Only valid if kind is Kind.ADVANCE and
      may be None.
    payload: A payload value to pass to a call of the advance method of the
      operator under test. Only valid if kind is Kind.ADVANCE and may be None.
    completion: A base.Completion value to pass to a call of the advance method
      of the operator under test. Only valid if kind is Kind.ADVANCE and may be
      None.
    allowance: An allowance value to pass to a call of the advance method of the
      operator under test. Only valid if kind is Kind.ADVANCE and may be None.
  """

  @enum.unique
  class Kind(enum.Enum):
    ADVANCE = 'advance'
    DEFECT = 'defect'
    IDLE = 'idle'


_DEFECT_ON_ADVANCE = OnAdvance(OnAdvance.Kind.DEFECT, None, None, None, None)
_IDLE_ON_ADVANCE = OnAdvance(OnAdvance.Kind.IDLE, None, None, None, None)


class Instruction(
    collections.namedtuple(
        'Instruction',
        ('kind', 'advance_args', 'advance_kwargs', 'conclude_success',
         'conclude_message', 'conclude_invocation_outcome_kind',
         'conclude_service_outcome_kind',))):
  """"""

  @enum.unique
  class Kind(enum.Enum):
    ADVANCE = 'ADVANCE'
    CANCEL = 'CANCEL'
    CONCLUDE = 'CONCLUDE'


class Controller(six.with_metaclass(abc.ABCMeta)):

  @abc.abstractmethod
  def failed(self, message):
    """"""
    raise NotImplementedError()

  @abc.abstractmethod
  def serialize_request(self, request):
    """"""
    raise NotImplementedError()

  @abc.abstractmethod
  def deserialize_request(self, serialized_request):
    """"""
    raise NotImplementedError()

  @abc.abstractmethod
  def serialize_response(self, response):
    """"""
    raise NotImplementedError()

  @abc.abstractmethod
  def deserialize_response(self, serialized_response):
    """"""
    raise NotImplementedError()

  @abc.abstractmethod
  def invocation(self):
    """"""
    raise NotImplementedError()

  @abc.abstractmethod
  def poll(self):
    """"""
    raise NotImplementedError()

  @abc.abstractmethod
  def on_service_advance(
      self, initial_metadata, payload, completion, allowance):
    """"""
    raise NotImplementedError()

  @abc.abstractmethod
  def on_invocation_advance(
      self, initial_metadata, payload, completion, allowance):
    """"""
    raise NotImplementedError()

  @abc.abstractmethod
  def service_on_termination(self, outcome):
    """"""
    raise NotImplementedError()

  @abc.abstractmethod
  def invocation_on_termination(self, outcome):
    """"""
    raise NotImplementedError()


class ControllerCreator(six.with_metaclass(abc.ABCMeta)):

  @abc.abstractmethod
  def name(self):
    """"""
    raise NotImplementedError()

  @abc.abstractmethod
  def controller(self, implementation, randomness):
    """"""
    raise NotImplementedError()


class _Remainder(
    collections.namedtuple(
        '_Remainder',
        ('invocation_payloads', 'service_payloads', 'invocation_completion',
         'service_completion',))):
  """Describes work remaining to be done in a portion of a test.

  Attributes:
    invocation_payloads: The number of payloads to be sent from the invocation
      side of the operation to the service side of the operation.
    service_payloads: The number of payloads to be sent from the service side of
      the operation to the invocation side of the operation.
    invocation_completion: Whether or not completion from the invocation side of
      the operation should be indicated and has yet to be indicated.
    service_completion: Whether or not completion from the service side of the
      operation should be indicated and has yet to be indicated.
  """


class _SequenceController(Controller):

  def __init__(self, sequence, implementation, randomness):
    """Constructor.

    Args:
      sequence: A _sequence.Sequence describing the steps to be taken in the
        test at a relatively high level.
      implementation: A test_interfaces.Implementation encapsulating the
        base interface implementation that is the system under test.
      randomness: A random.Random instance for use in the test.
    """
    self._condition = threading.Condition()
    self._sequence = sequence
    self._implementation = implementation
    self._randomness = randomness

    self._until = None
    self._remaining_elements = None
    self._poll_next = None
    self._message = None

    self._state = _state.OperationState()
    self._todo = None

  # called with self._condition
  def _failed(self, message):
    self._message = message
    self._condition.notify_all()

  def _passed(self, invocation_outcome, service_outcome):
    self._poll_next = Instruction(
        Instruction.Kind.CONCLUDE, None, None, True, None, invocation_outcome,
        service_outcome)
    self._condition.notify_all()

  def failed(self, message):
    with self._condition:
      self._failed(message)

  def serialize_request(self, request):
    return request + request

  def deserialize_request(self, serialized_request):
    return serialized_request[:len(serialized_request) // 2]

  def serialize_response(self, response):
    return response * 3

  def deserialize_response(self, serialized_response):
    return serialized_response[2 * len(serialized_response) // 3:]

  def invocation(self):
    with self._condition:
      self._until = time.time() + self._sequence.maximum_duration
      self._remaining_elements = list(self._sequence.elements)
      if self._sequence.invocation.initial_metadata:
        initial_metadata = self._implementation.invocation_initial_metadata()
        self._state.invocation_initial_metadata_in_flight = initial_metadata
      else:
        initial_metadata = None
      if self._sequence.invocation.payload:
        payload = _create_payload(self._randomness)
        self._state.invocation_payloads_in_flight.append(payload)
      else:
        payload = None
      if self._sequence.invocation.complete:
        completion = self._implementation.invocation_completion()
        self._state.invocation_completion_in_flight = completion
      else:
        completion = None
      return Invocation(
          _GROUP, _METHOD, base.Subscription.Kind.FULL,
          self._sequence.invocation.timeout, initial_metadata, payload,
          completion)

  def poll(self):
    with self._condition:
      while True:
        if self._message is not None:
          return Instruction(
              Instruction.Kind.CONCLUDE, None, None, False, self._message, None,
              None)
        elif self._poll_next:
          poll_next = self._poll_next
          self._poll_next = None
          return poll_next
        elif self._until < time.time():
          return Instruction(
              Instruction.Kind.CONCLUDE, None, None, False,
              'overran allotted time!', None, None)
        else:
          self._condition.wait(timeout=self._until-time.time())

  def on_service_advance(
      self, initial_metadata, payload, completion, allowance):
    with self._condition:
      message = _verify_service_advance_and_update_state(
          initial_metadata, payload, completion, allowance, self._state,
          self._implementation)
      if message is not None:
        self._failed(message)
      if self._todo is not None:
        raise ValueError('TODO!!!')
      elif _anything_in_flight(self._state):
        return _IDLE_ON_ADVANCE
      elif self._remaining_elements:
        element = self._remaining_elements.pop(0)
        if element.kind is _sequence.Element.Kind.SERVICE_TRANSMISSION:
          if element.transmission.initial_metadata:
            initial_metadata = self._implementation.service_initial_metadata()
            self._state.service_initial_metadata_in_flight = initial_metadata
          else:
            initial_metadata = None
          if element.transmission.payload:
            payload = _create_payload(self._randomness)
            self._state.service_payloads_in_flight.append(payload)
            self._state.service_side_service_allowance -= 1
          else:
            payload = None
          if element.transmission.complete:
            completion = self._implementation.service_completion()
            self._state.service_completion_in_flight = completion
          else:
            completion = None
          if (not self._state.invocation_completion_received and
              0 <= self._state.service_side_invocation_allowance):
            allowance = 1
            self._state.service_side_invocation_allowance += 1
            self._state.invocation_allowance_in_flight += 1
          else:
            allowance = None
          return OnAdvance(
              OnAdvance.Kind.ADVANCE, initial_metadata, payload, completion,
              allowance)
        else:
          raise ValueError('TODO!!!')
      else:
        return _IDLE_ON_ADVANCE

  def on_invocation_advance(
      self, initial_metadata, payload, completion, allowance):
    with self._condition:
      message = _verify_invocation_advance_and_update_state(
          initial_metadata, payload, completion, allowance, self._state,
          self._implementation)
      if message is not None:
        self._failed(message)
      if self._todo is not None:
        raise ValueError('TODO!!!')
      elif _anything_in_flight(self._state):
        return _IDLE_ON_ADVANCE
      elif self._remaining_elements:
        element = self._remaining_elements.pop(0)
        if element.kind is _sequence.Element.Kind.INVOCATION_TRANSMISSION:
          if element.transmission.initial_metadata:
            initial_metadata = self._implementation.invocation_initial_metadata()
            self._state.invocation_initial_metadata_in_fight = initial_metadata
          else:
            initial_metadata = None
          if element.transmission.payload:
            payload = _create_payload(self._randomness)
            self._state.invocation_payloads_in_flight.append(payload)
            self._state.invocation_side_invocation_allowance -= 1
          else:
            payload = None
          if element.transmission.complete:
            completion = self._implementation.invocation_completion()
            self._state.invocation_completion_in_flight = completion
          else:
            completion = None
          if (not self._state.service_completion_received and
              0 <= self._state.invocation_side_service_allowance):
            allowance = 1
            self._state.invocation_side_service_allowance += 1
            self._state.service_allowance_in_flight += 1
          else:
            allowance = None
          return OnAdvance(
              OnAdvance.Kind.ADVANCE, initial_metadata, payload, completion,
              allowance)
        else:
          raise ValueError('TODO!!!')
      else:
        return _IDLE_ON_ADVANCE

  def service_on_termination(self, outcome):
    with self._condition:
      self._state.service_side_outcome = outcome
      if self._todo is not None or self._remaining_elements:
        self._failed('Premature service-side outcome %s!' % (outcome,))
      elif outcome.kind is not self._sequence.outcome_kinds.service:
        self._failed(
            'Incorrect service-side outcome kind: %s should have been %s' % (
                outcome.kind, self._sequence.outcome_kinds.service))
      elif self._state.invocation_side_outcome is not None:
        self._passed(self._state.invocation_side_outcome.kind, outcome.kind)

  def invocation_on_termination(self, outcome):
    with self._condition:
      self._state.invocation_side_outcome = outcome
      if self._todo is not None or self._remaining_elements:
        self._failed('Premature invocation-side outcome %s!' % (outcome,))
      elif outcome.kind is not self._sequence.outcome_kinds.invocation:
        self._failed(
            'Incorrect invocation-side outcome kind: %s should have been %s' % (
                outcome.kind, self._sequence.outcome_kinds.invocation))
      elif self._state.service_side_outcome is not None:
        self._passed(outcome.kind, self._state.service_side_outcome.kind)


class _SequenceControllerCreator(ControllerCreator):

  def __init__(self, sequence):
    self._sequence = sequence

  def name(self):
    return self._sequence.name

  def controller(self, implementation, randomness):
    return _SequenceController(self._sequence, implementation, randomness)


CONTROLLER_CREATORS = tuple(
    _SequenceControllerCreator(sequence) for sequence in _sequence.SEQUENCES)
