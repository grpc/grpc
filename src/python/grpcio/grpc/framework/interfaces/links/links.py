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

"""The low-level ticket-exchanging-links interface of RPC Framework."""

import abc
import collections
import enum

import six


class Protocol(collections.namedtuple('Protocol', ('kind', 'value',))):
  """A sum type for handles to a system that transmits tickets.

  Attributes:
    kind: A Kind value identifying the kind of value being passed.
    value: The value being passed between the high-level application and the
      system affording ticket transport.
  """

  @enum.unique
  class Kind(enum.Enum):
    CALL_OPTION = 'call option'
    SERVICER_CONTEXT = 'servicer context'
    INVOCATION_CONTEXT = 'invocation context'


class Ticket(
    collections.namedtuple(
        'Ticket',
        ('operation_id', 'sequence_number', 'group', 'method', 'subscription',
         'timeout', 'allowance', 'initial_metadata', 'payload',
         'terminal_metadata', 'code', 'message', 'termination', 'protocol',))):
  """A sum type for all values sent from a front to a back.

  Attributes:
    operation_id: A unique-with-respect-to-equality hashable object identifying
      a particular operation.
    sequence_number: A zero-indexed integer sequence number identifying the
      ticket's place in the stream of tickets sent in one direction for the
      particular operation.
    group: The group to which the method of the operation belongs. Must be
      present in the first ticket from invocation side to service side. Ignored
      for all other tickets exchanged during the operation.
    method: The name of an operation. Must be present in the first ticket from
      invocation side to service side. Ignored for all other tickets exchanged
      during the operation.
    subscription: A Subscription value describing the interest one side has in
      receiving information from the other side. Must be present in the first
      ticket from either side. Ignored for all other tickets exchanged during
      the operation.
    timeout: A nonzero length of time (measured from the beginning of the
      operation) to allow for the entire operation. Must be present in the first
      ticket from invocation side to service side. Optional for all other
      tickets exchanged during the operation. Receipt of a value from the other
      side of the operation indicates the value in use by that side. Setting a
      value on a later ticket allows either side to request time extensions (or
      even time reductions!) on in-progress operations.
    allowance: A positive integer granting permission for a number of payloads
      to be transmitted to the communicating side of the operation, or None if
      no additional allowance is being granted with this ticket.
    initial_metadata: An optional metadata value communicated from one side to
      the other at the beginning of the operation. May be non-None in at most
      one ticket from each side. Any non-None value must appear no later than
      the first payload value.
    payload: A customer payload object. May be None.
    terminal_metadata: A metadata value comminicated from one side to the other
      at the end of the operation. May be non-None in the same ticket as
      the code and message, but must be None for all earlier tickets.
    code: A value communicated at operation completion. May be None.
    message: A value communicated at operation completion. May be None.
    termination: A Termination value describing the end of the operation, or
      None if the operation has not yet terminated. If set, no further tickets
      may be sent in the same direction.
    protocol: A Protocol value or None, with further semantics being a matter
      between high-level application and underlying ticket transport.
  """

  @enum.unique
  class Subscription(enum.Enum):
    """Identifies the level of subscription of a side of an operation."""

    NONE = 'none'
    TERMINATION = 'termination'
    FULL = 'full'

  @enum.unique
  class Termination(enum.Enum):
    """Identifies the termination of an operation."""

    COMPLETION = 'completion'
    CANCELLATION = 'cancellation'
    EXPIRATION = 'expiration'
    SHUTDOWN = 'shutdown'
    RECEPTION_FAILURE = 'reception failure'
    TRANSMISSION_FAILURE = 'transmission failure'
    LOCAL_FAILURE = 'local failure'
    REMOTE_FAILURE = 'remote failure'


class Link(six.with_metaclass(abc.ABCMeta)):
  """Accepts and emits tickets."""

  @abc.abstractmethod
  def accept_ticket(self, ticket):
    """Accept a Ticket.

    Args:
      ticket: Any Ticket.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def join_link(self, link):
    """Mates this object with a peer with which it will exchange tickets."""
    raise NotImplementedError()
