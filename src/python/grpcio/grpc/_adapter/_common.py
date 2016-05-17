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

"""State used by both invocation-side and service-side code."""

import enum


@enum.unique
class HighWrite(enum.Enum):
  """The possible categories of high-level write state."""

  OPEN = 'OPEN'
  CLOSED = 'CLOSED'


class WriteState(object):
  """A description of the state of writing to an RPC.

  Attributes:
    low: A side-specific value describing the low-level state of writing.
    high: A HighWrite value describing the high-level state of writing.
    pending: A list of bytestrings for the RPC waiting to be written to the
      other side of the RPC.
  """

  def __init__(self, low, high, pending):
    self.low = low
    self.high = high
    self.pending = pending


class CommonRPCState(object):
  """A description of an RPC's state.

  Attributes:
    write: A WriteState describing the state of writing to the RPC.
    sequence_number: The lowest-unused sequence number for use in generating
      tickets locally describing the progress of the RPC.
    deserializer: The behavior to be used to deserialize payload bytestreams
      taken off the wire.
    serializer: The behavior to be used to serialize payloads to be sent on the
      wire.
  """

  def __init__(self, write, sequence_number, deserializer, serializer):
    self.write = write
    self.sequence_number = sequence_number
    self.deserializer = deserializer
    self.serializer = serializer
