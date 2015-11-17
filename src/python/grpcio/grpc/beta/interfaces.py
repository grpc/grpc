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

from grpc._adapter import _types


@enum.unique
class ChannelConnectivity(enum.Enum):
  """Mirrors grpc_connectivity_state in the gRPC Core.

  Attributes:
    IDLE: The channel is idle.
    CONNECTING: The channel is connecting.
    READY: The channel is ready to conduct RPCs.
    TRANSIENT_FAILURE: The channel has seen a failure from which it expects to
      recover.
    FATAL_FAILURE: The channel has seen a failure from which it cannot recover.
  """
  IDLE = (_types.ConnectivityState.IDLE, 'idle',)
  CONNECTING = (_types.ConnectivityState.CONNECTING, 'connecting',)
  READY = (_types.ConnectivityState.READY, 'ready',)
  TRANSIENT_FAILURE = (
      _types.ConnectivityState.TRANSIENT_FAILURE, 'transient failure',)
  FATAL_FAILURE = (_types.ConnectivityState.FATAL_FAILURE, 'fatal failure',)


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
    credentials: Reserved for gRPC per-call credentials. The type for this does
      not exist yet at the Python level.
  """
  if credentials is not None:
    raise ValueError('`credentials` is a reserved argument')
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


class Server(object):
  """Services RPCs."""
  __metaclass__ = abc.ABCMeta

  @abc.abstractmethod
  def add_insecure_port(self, address):
    """Reserves a port for insecure RPC service once this Server becomes active.

    This method may only be called before calling this Server's start method is
    called.

    Args:
      address: The address for which to open a port.

    Returns:
      An integer port on which RPCs will be serviced after this link has been
        started. This is typically the same number as the port number contained
        in the passed address, but will likely be different if the port number
        contained in the passed address was zero.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def add_secure_port(self, address, server_credentials):
    """Reserves a port for secure RPC service after this Server becomes active.

    This method may only be called before calling this Server's start method is
    called.

    Args:
      address: The address for which to open a port.
      server_credentials: A ServerCredentials.

    Returns:
      An integer port on which RPCs will be serviced after this link has been
        started. This is typically the same number as the port number contained
        in the passed address, but will likely be different if the port number
        contained in the passed address was zero.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def start(self):
    """Starts this Server's service of RPCs.

    This method may only be called while the server is not serving RPCs (i.e. it
    is not idempotent).
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def stop(self, grace):
    """Stops this Server's service of RPCs.

    All calls to this method immediately stop service of new RPCs. When existing
    RPCs are aborted is controlled by the grace period parameter passed to this
    method.

    This method may be called at any time and is idempotent. Passing a smaller
    grace value than has been passed in a previous call will have the effect of
    stopping the Server sooner. Passing a larger grace value than has been
    passed in a previous call will not have the effect of stopping the sooner
    later.

    Args:
      grace: A duration of time in seconds to allow existing RPCs to complete
        before being aborted by this Server's stopping. May be zero for
        immediate abortion of all in-progress RPCs.

    Returns:
      A threading.Event that will be set when this Server has completely
      stopped. The returned event may not be set until after the full grace
      period (if some ongoing RPC continues for the full length of the period)
      of it may be set much sooner (such as if this Server had no RPCs underway
      at the time it was stopped or if all RPCs that it had underway completed
      very early in the grace period).
    """
    raise NotImplementedError()
