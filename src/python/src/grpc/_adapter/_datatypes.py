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

"""Datatypes passed between Python and C code."""

import collections
import enum


@enum.unique
class Code(enum.IntEnum):
  """One Platform error codes (see status.h and codes.proto)."""

  OK = 0
  CANCELLED = 1
  UNKNOWN = 2
  INVALID_ARGUMENT = 3
  EXPIRED = 4
  NOT_FOUND = 5
  ALREADY_EXISTS = 6
  PERMISSION_DENIED = 7
  UNAUTHENTICATED = 16
  RESOURCE_EXHAUSTED = 8
  FAILED_PRECONDITION = 9
  ABORTED = 10
  OUT_OF_RANGE = 11
  UNIMPLEMENTED = 12
  INTERNAL_ERROR = 13
  UNAVAILABLE = 14
  DATA_LOSS = 15


class Status(collections.namedtuple('Status', ['code', 'details'])):
  """Describes an RPC's overall status."""


class ServiceAcceptance(
    collections.namedtuple(
        'ServiceAcceptance', ['call', 'method', 'host', 'deadline'])):
  """Describes an RPC on the service side at the start of service."""


class Event(
    collections.namedtuple(
        'Event',
        ['kind', 'tag', 'write_accepted', 'complete_accepted',
         'service_acceptance', 'bytes', 'status', 'metadata'])):
  """Describes an event emitted from a completion queue."""

  @enum.unique
  class Kind(enum.Enum):
    """Describes the kind of an event."""

    STOP = object()
    WRITE_ACCEPTED = object()
    COMPLETE_ACCEPTED = object()
    SERVICE_ACCEPTED = object()
    READ_ACCEPTED = object()
    METADATA_ACCEPTED = object()
    FINISH = object()
