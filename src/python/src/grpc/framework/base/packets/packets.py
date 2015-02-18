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

"""Packets used between fronts and backs."""

import collections
import enum

# interfaces is referenced from specifications in this module.
from grpc.framework.base import interfaces  # pylint: disable=unused-import


@enum.unique
class Kind(enum.Enum):
  """Identifies the overall kind of a ticket."""

  COMMENCEMENT = 'commencement'
  CONTINUATION = 'continuation'
  COMPLETION = 'completion'
  ENTIRE = 'entire'
  CANCELLATION = 'cancellation'
  EXPIRATION = 'expiration'
  SERVICER_FAILURE = 'servicer failure'
  SERVICED_FAILURE = 'serviced failure'
  RECEPTION_FAILURE = 'reception failure'
  TRANSMISSION_FAILURE = 'transmission failure'


class FrontToBackPacket(
    collections.namedtuple(
        'FrontToBackPacket',
        ['operation_id', 'sequence_number', 'kind', 'name', 'subscription',
         'trace_id', 'payload', 'timeout'])):
  """A sum type for all values sent from a front to a back.

  Attributes:
    operation_id: A unique-with-respect-to-equality hashable object identifying
      a particular operation.
    sequence_number: A zero-indexed integer sequence number identifying the
      packet's place among all the packets sent from front to back for this
      particular operation. Must be zero if kind is Kind.COMMENCEMENT or
      Kind.ENTIRE. Must be positive for any other kind.
    kind: One of Kind.COMMENCEMENT, Kind.CONTINUATION, Kind.COMPLETION,
      Kind.ENTIRE, Kind.CANCELLATION, Kind.EXPIRATION, Kind.SERVICED_FAILURE,
      Kind.RECEPTION_FAILURE, or Kind.TRANSMISSION_FAILURE.
    name: The name of an operation. Must be present if kind is Kind.COMMENCEMENT
      or Kind.ENTIRE. Must be None for any other kind.
    subscription: An interfaces.ServicedSubscription.Kind value describing the
      interest the front has in packets sent from the back. Must be present if
      kind is Kind.COMMENCEMENT or Kind.ENTIRE. Must be None for any other kind.
    trace_id: A uuid.UUID identifying a set of related operations to which this
      operation belongs. May be None.
    payload: A customer payload object. Must be present if kind is
      Kind.CONTINUATION. Must be None if kind is Kind.CANCELLATION. May be None
      for any other kind.
    timeout: An optional length of time (measured from the beginning of the
      operation) to allow for the entire operation. If None, a default value on
      the back will be used. If present and excessively large, the back may
      limit the operation to a smaller duration of its choice. May be present
      for any ticket kind; setting a value on a later ticket allows fronts
      to request time extensions (or even time reductions!) on in-progress
      operations.
  """


class BackToFrontPacket(
    collections.namedtuple(
        'BackToFrontPacket',
        ['operation_id', 'sequence_number', 'kind', 'payload'])):
  """A sum type for all values sent from a back to a front.

  Attributes:
    operation_id: A unique-with-respect-to-equality hashable object identifying
      a particular operation.
    sequence_number: A zero-indexed integer sequence number identifying the
      packet's place among all the packets sent from back to front for this
      particular operation.
    kind: One of Kind.CONTINUATION, Kind.COMPLETION, Kind.EXPIRATION,
      Kind.SERVICER_FAILURE, Kind.RECEPTION_FAILURE, or
      Kind.TRANSMISSION_FAILURE.
    payload: A customer payload object. Must be present if kind is
      Kind.CONTINUATION. May be None if kind is Kind.COMPLETION. Must be None if
      kind is Kind.EXPIRATION, Kind.SERVICER_FAILURE, Kind.RECEPTION_FAILURE, or
      Kind.TRANSMISSION_FAILURE.
  """
