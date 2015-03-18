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

"""Interfaces of GRPC."""

import abc
import enum

# exceptions is referenced from specification in this module.
from grpc.framework.alpha import exceptions  # pylint: disable=unused-import
from grpc.framework.foundation import activated
from grpc.framework.foundation import future


@enum.unique
class Cardinality(enum.Enum):
  """Constants for the four cardinalities of RPC."""

  UNARY_UNARY = 'request-unary/response-unary'
  UNARY_STREAM = 'request-unary/response-streaming'
  STREAM_UNARY = 'request-streaming/response-unary'
  STREAM_STREAM = 'request-streaming/response-streaming'


@enum.unique
class Abortion(enum.Enum):
  """Categories of RPC abortion."""

  CANCELLED = 'cancelled'
  EXPIRED = 'expired'
  NETWORK_FAILURE = 'network failure'
  SERVICED_FAILURE = 'serviced failure'
  SERVICER_FAILURE = 'servicer failure'


class CancellableIterator(object):
  """Implements the Iterator protocol and affords a cancel method."""
  __metaclass__ = abc.ABCMeta

  @abc.abstractmethod
  def __iter__(self):
    """Returns the self object in accordance with the Iterator protocol."""
    raise NotImplementedError()

  @abc.abstractmethod
  def next(self):
    """Returns a value or raises StopIteration per the Iterator protocol."""
    raise NotImplementedError()

  @abc.abstractmethod
  def cancel(self):
    """Requests cancellation of whatever computation underlies this iterator."""
    raise NotImplementedError()


class RpcContext(object):
  """Provides RPC-related information and action."""
  __metaclass__ = abc.ABCMeta

  @abc.abstractmethod
  def is_active(self):
    """Describes whether the RPC is active or has terminated."""
    raise NotImplementedError()

  @abc.abstractmethod
  def time_remaining(self):
    """Describes the length of allowed time remaining for the RPC.
    Returns:
      A nonnegative float indicating the length of allowed time in seconds
      remaining for the RPC to complete before it is considered to have timed
      out.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def add_abortion_callback(self, abortion_callback):
    """Registers a callback to be called if the RPC is aborted.
    Args:
      abortion_callback: A callable to be called and passed an Abortion value
        in the event of RPC abortion.
    """
    raise NotImplementedError()


class UnaryUnarySyncAsync(object):
  """Affords invoking a unary-unary RPC synchronously or asynchronously.
  Values implementing this interface are directly callable and present an
  "async" method. Both calls take a request value and a numeric timeout.
  Direct invocation of a value of this type invokes its associated RPC and
  blocks until the RPC's response is available. Calling the "async" method
  of a value of this type invokes its associated RPC and immediately returns a
  future.Future bound to the asynchronous execution of the RPC.
  """
  __metaclass__ = abc.ABCMeta

  @abc.abstractmethod
  def __call__(self, request, timeout):
    """Synchronously invokes the underlying RPC.
    Args:
      request: The request value for the RPC.
      timeout: A duration of time in seconds to allow for the RPC.
    Returns:
      The response value for the RPC.
    Raises:
      exceptions.RpcError: Indicating that the RPC was aborted.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def async(self, request, timeout):
    """Asynchronously invokes the underlying RPC.
    Args:
      request: The request value for the RPC.
      timeout: A duration of time in seconds to allow for the RPC.
    Returns:
      A future.Future representing the RPC. In the event of RPC completion, the
        returned Future's result value will be the response value of the RPC.
        In the event of RPC abortion, the returned Future's exception value
        will be an exceptions.RpcError.
    """
    raise NotImplementedError()


class StreamUnarySyncAsync(object):
  """Affords invoking a stream-unary RPC synchronously or asynchronously.
  Values implementing this interface are directly callable and present an
  "async" method. Both calls take an iterator of request values and a numeric
  timeout. Direct invocation of a value of this type invokes its associated RPC
  and blocks until the RPC's response is available. Calling the "async" method
  of a value of this type invokes its associated RPC and immediately returns a
  future.Future bound to the asynchronous execution of the RPC.
  """
  __metaclass__ = abc.ABCMeta

  @abc.abstractmethod
  def __call__(self, request_iterator, timeout):
    """Synchronously invokes the underlying RPC.

    Args:
      request_iterator: An iterator that yields request values for the RPC.
      timeout: A duration of time in seconds to allow for the RPC.

    Returns:
      The response value for the RPC.

    Raises:
      exceptions.RpcError: Indicating that the RPC was aborted.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def async(self, request_iterator, timeout):
    """Asynchronously invokes the underlying RPC.

    Args:
      request_iterator: An iterator that yields request values for the RPC.
      timeout: A duration of time in seconds to allow for the RPC.

    Returns:
      A future.Future representing the RPC. In the event of RPC completion, the
        returned Future's result value will be the response value of the RPC.
        In the event of RPC abortion, the returned Future's exception value
        will be an exceptions.RpcError.
    """
    raise NotImplementedError()


class RpcMethodDescription(object):
  """A type for the common aspects of RPC method descriptions."""
  __metaclass__ = abc.ABCMeta

  @abc.abstractmethod
  def cardinality(self):
    """Identifies the cardinality of this RpcMethodDescription.

    Returns:
      A Cardinality value identifying whether or not this
        RpcMethodDescription is request-unary or request-streaming and
        whether or not it is response-unary or response-streaming.
    """
    raise NotImplementedError()


class RpcMethodInvocationDescription(RpcMethodDescription):
  """Invocation-side description of an RPC method."""
  __metaclass__ = abc.ABCMeta

  @abc.abstractmethod
  def serialize_request(self, request):
    """Serializes a request value.

    Args:
      request: A request value appropriate for the RPC method described by this
        RpcMethodInvocationDescription.

    Returns:
      The serialization of the given request value as a
        bytestring.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def deserialize_response(self, serialized_response):
    """Deserializes a response value.

    Args:
      serialized_response: A bytestring that is the serialization of a response
        value appropriate for the RPC method described by this
        RpcMethodInvocationDescription.

    Returns:
      A response value corresponding to the given bytestring.
    """
    raise NotImplementedError()


class RpcMethodServiceDescription(RpcMethodDescription):
  """Service-side description of an RPC method."""
  __metaclass__ = abc.ABCMeta

  @abc.abstractmethod
  def deserialize_request(self, serialized_request):
    """Deserializes a request value.

    Args:
      serialized_request: A bytestring that is the serialization of a request
        value appropriate for the RPC method described by this
        RpcMethodServiceDescription.

    Returns:
      A request value corresponding to the given bytestring.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def serialize_response(self, response):
    """Serializes a response value.

    Args:
      response: A response value appropriate for the RPC method described by
        this RpcMethodServiceDescription.

    Returns:
      The serialization of the given response value as a
        bytestring.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def service_unary_unary(self, request, context):
    """Carries out this RPC.

    This method may only be called if the cardinality of this
    RpcMethodServiceDescription is Cardinality.UNARY_UNARY.

    Args:
      request: A request value appropriate for the RPC method described by this
        RpcMethodServiceDescription.
      context: An RpcContext object for the RPC.

    Returns:
      A response value appropriate for the RPC method described by this
        RpcMethodServiceDescription.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def service_unary_stream(self, request, context):
    """Carries out this RPC.

    This method may only be called if the cardinality of this
    RpcMethodServiceDescription is Cardinality.UNARY_STREAM.

    Args:
      request: A request value appropriate for the RPC method described by this
        RpcMethodServiceDescription.
      context: An RpcContext object for the RPC.

    Yields:
      Zero or more response values appropriate for the RPC method described by
        this RpcMethodServiceDescription.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def service_stream_unary(self, request_iterator, context):
    """Carries out this RPC.

    This method may only be called if the cardinality of this
    RpcMethodServiceDescription is Cardinality.STREAM_UNARY.

    Args:
      request_iterator: An iterator of request values appropriate for the RPC
        method described by this RpcMethodServiceDescription.
      context: An RpcContext object for the RPC.

    Returns:
      A response value appropriate for the RPC method described by this
        RpcMethodServiceDescription.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def service_stream_stream(self, request_iterator, context):
    """Carries out this RPC.

    This method may only be called if the cardinality of this
    RpcMethodServiceDescription is Cardinality.STREAM_STREAM.

    Args:
      request_iterator: An iterator of request values appropriate for the RPC
        method described by this RpcMethodServiceDescription.
      context: An RpcContext object for the RPC.

    Yields:
      Zero or more response values appropriate for the RPC method described by
        this RpcMethodServiceDescription.
    """
    raise NotImplementedError()


class Stub(object):
  """A stub with callable RPC method names for attributes.

  Instances of this type are context managers and only afford RPC invocation
  when used in context.

  Instances of this type, when used in context, respond to attribute access
  as follows: if the requested attribute is the name of a unary-unary RPC
  method, the value of the attribute will be a UnaryUnarySyncAsync with which
  to invoke the RPC method. If the requested attribute is the name of a
  unary-stream RPC method, the value of the attribute will be a callable taking
  a request object and a timeout parameter and returning a CancellableIterator
  that yields the response values of the RPC. If the requested attribute is the
  name of a stream-unary RPC method, the value of the attribute will be a
  StreamUnarySyncAsync with which to invoke the RPC method. If the requested
  attribute is the name of a stream-stream RPC method, the value of the
  attribute will be a callable taking an iterator of request objects and a
  timeout and returning a CancellableIterator that yields the response values
  of the RPC.

  In all cases indication of abortion is indicated by raising of
  exceptions.RpcError, exceptions.CancellationError,
  and exceptions.ExpirationError.
  """
  __metaclass__ = abc.ABCMeta


class Server(activated.Activated):
  """A GRPC Server."""
  __metaclass__ = abc.ABCMeta

  @abc.abstractmethod
  def port(self):
    """Reports the port on which the server is serving.

    This method may only be called while the server is activated.

    Returns:
      The port on which the server is serving.
    """
    raise NotImplementedError()
