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

import collections
import enum

from grpc.framework.interfaces.base import base
from tests.unit.framework.common import test_constants


class Invocation(
    collections.namedtuple(
        'Invocation', ('timeout', 'initial_metadata', 'payload', 'complete',))):
  """A recipe for operation invocation.

  Attributes:
    timeout: A duration in seconds to pass to the system under test as the
      operation's timeout value.
    initial_metadata: A boolean indicating whether or not to pass initial
      metadata when invoking the operation.
    payload: A boolean indicating whether or not to pass a payload when
      invoking the operation.
    complete: A boolean indicating whether or not to indicate completion of
      transmissions from the invoking side of the operation when invoking the
      operation.
  """


class Transmission(
    collections.namedtuple(
        'Transmission', ('initial_metadata', 'payload', 'complete',))):
  """A recipe for a single transmission in an operation.

  Attributes:
    initial_metadata: A boolean indicating whether or not to pass initial
      metadata as part of the transmission.
    payload: A boolean indicating whether or not to pass a payload as part of
      the transmission.
    complete: A boolean indicating whether or not to indicate completion of
      transmission from the transmitting side of the operation as part of the
      transmission.
  """


class Intertransmission(
    collections.namedtuple('Intertransmission', ('invocation', 'service',))):
  """A recipe for multiple transmissions in an operation.

  Attributes:
    invocation: An integer describing the number of payloads to send from the
      invocation side of the operation to the service side.
    service: An integer describing the number of payloads to send from the
      service side of the operation to the invocation side.
  """


class Element(collections.namedtuple('Element', ('kind', 'transmission',))):
  """A sum type for steps to perform when testing an operation.

  Attributes:
    kind: A Kind value describing the kind of step to perform in the test.
    transmission: Only valid for kinds Kind.INVOCATION_TRANSMISSION and
      Kind.SERVICE_TRANSMISSION, a Transmission value describing the details of
      the transmission to be made.
  """

  @enum.unique
  class Kind(enum.Enum):
    INVOCATION_TRANSMISSION = 'invocation transmission'
    SERVICE_TRANSMISSION = 'service transmission'
    INTERTRANSMISSION = 'intertransmission'
    INVOCATION_CANCEL = 'invocation cancel'
    SERVICE_CANCEL = 'service cancel'
    INVOCATION_FAILURE = 'invocation failure'
    SERVICE_FAILURE = 'service failure'


class OutcomeKinds(
    collections.namedtuple('Outcome', ('invocation', 'service',))):
  """A description of the expected outcome of an operation test.

  Attributes:
    invocation: The base.Outcome.Kind value expected on the invocation side of
      the operation.
    service: The base.Outcome.Kind value expected on the service side of the
      operation.
  """


class Sequence(
    collections.namedtuple(
        'Sequence',
        ('name', 'maximum_duration', 'invocation', 'elements',
         'outcome_kinds',))):
  """Describes at a high level steps to perform in a test.

  Attributes:
    name: The string name of the sequence.
    maximum_duration: A length of time in seconds to allow for the test before
      declaring it to have failed.
    invocation: An Invocation value describing how to invoke the operation
      under test.
    elements: A sequence of Element values describing at coarse granularity
      actions to take during the operation under test.
    outcome_kinds: An OutcomeKinds value describing the expected outcome kinds
      of the test.
  """

_EASY = Sequence(
    'Easy',
    test_constants.TIME_ALLOWANCE,
    Invocation(test_constants.LONG_TIMEOUT, True, True, True),
    (
        Element(
            Element.Kind.SERVICE_TRANSMISSION, Transmission(True, True, True)),
    ),
    OutcomeKinds(base.Outcome.Kind.COMPLETED, base.Outcome.Kind.COMPLETED))

_PEASY = Sequence(
    'Peasy',
    test_constants.TIME_ALLOWANCE,
    Invocation(test_constants.LONG_TIMEOUT, True, True, False),
    (
        Element(
            Element.Kind.SERVICE_TRANSMISSION, Transmission(True, True, False)),
        Element(
            Element.Kind.INVOCATION_TRANSMISSION,
            Transmission(False, True, True)),
        Element(
            Element.Kind.SERVICE_TRANSMISSION, Transmission(False, True, True)),
    ),
    OutcomeKinds(base.Outcome.Kind.COMPLETED, base.Outcome.Kind.COMPLETED))


# TODO(issue 2959): Finish this test suite. This tuple of sequences should
# contain at least the values in the Cartesian product of (half-duplex,
# full-duplex) * (zero payloads, one payload, test_constants.STREAM_LENGTH
# payloads) * (completion, cancellation, expiration, programming defect in
# servicer code).
SEQUENCES = (
    _EASY,
    _PEASY,
)
