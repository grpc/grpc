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

"""Constants and interfaces of the Beta API of gRPC Python."""

import abc
import enum


@enum.unique
class StatusCode(enum.Enum):
  """Mirrors grpc_status_code in the C core."""
  OK                  = 0
  CANCELLED           = 1
  UNKNOWN             = 2
  INVALID_ARGUMENT    = 3
  DEADLINE_EXCEEDED   = 4
  NOT_FOUND           = 5
  ALREADY_EXISTS      = 6
  PERMISSION_DENIED   = 7
  RESOURCE_EXHAUSTED  = 8
  FAILED_PRECONDITION = 9
  ABORTED             = 10
  OUT_OF_RANGE        = 11
  UNIMPLEMENTED       = 12
  INTERNAL            = 13
  UNAVAILABLE         = 14
  DATA_LOSS           = 15
  UNAUTHENTICATED     = 16


class GRPCCallOptions(object):
  """A value encapsulating gRPC-specific options passed on RPC invocation.

  This class and its instances have no supported interface - it exists to
  define the type of its instances and its instances exist to be passed to
  other functions.
  """

  def __init__(self, disable_compression, subcall_of, credentials):
    self.disable_compression = disable_compression
    self.subcall_of = subcall_of
    self.credentials = credentials


def grpc_call_options(disable_compression=False, credentials=None):
  """Creates a GRPCCallOptions value to be passed at RPC invocation.

  All parameters are optional and should always be passed by keyword.

  Args:
    disable_compression: A boolean indicating whether or not compression should
      be disabled for the request object of the RPC. Only valid for
      request-unary RPCs.
    credentials: A ClientCredentials object to use for the invoked RPC.
  """
  return GRPCCallOptions(disable_compression, None, credentials)


class GRPCServicerContext(object):
  """Exposes gRPC-specific options and behaviors to code servicing RPCs."""
  __metaclass__ = abc.ABCMeta

  @abc.abstractmethod
  def peer(self):
    """Identifies the peer that invoked the RPC being serviced.

    Returns:
      A string identifying the peer that invoked the RPC being serviced.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def disable_next_response_compression(self):
    """Disables compression of the next response passed by the application."""
    raise NotImplementedError()


class GRPCInvocationContext(object):
  """Exposes gRPC-specific options and behaviors to code invoking RPCs."""
  __metaclass__ = abc.ABCMeta

  @abc.abstractmethod
  def disable_next_request_compression(self):
    """Disables compression of the next request passed by the application."""
    raise NotImplementedError()
