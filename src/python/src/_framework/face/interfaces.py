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

"""Interfaces for the face layer of RPC Framework."""

import abc
import enum

# exceptions, abandonment, and future are referenced from specification in this
# module.
from _framework.face import exceptions  # pylint: disable=unused-import
from _framework.foundation import abandonment  # pylint: disable=unused-import
from _framework.foundation import future  # pylint: disable=unused-import


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


@enum.unique
class Abortion(enum.Enum):
  """Categories of RPC abortion."""

  CANCELLED = 'cancelled'
  EXPIRED = 'expired'
  NETWORK_FAILURE = 'network failure'
  SERVICED_FAILURE = 'serviced failure'
  SERVICER_FAILURE = 'servicer failure'


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


class InlineValueInValueOutMethod(object):
  """A type for inline unary-request-unary-response RPC methods."""
  __metaclass__ = abc.ABCMeta

  @abc.abstractmethod
  def service(self, request, context):
    """Services an RPC that accepts one value and produces one value.

    Args:
      request: The single request value for the RPC.
      context: An RpcContext object.

    Returns:
      The single response value for the RPC.

    Raises:
      abandonment.Abandoned: If no response is necessary because the RPC has
        been aborted.
    """
    raise NotImplementedError()


class InlineValueInStreamOutMethod(object):
  """A type for inline unary-request-stream-response RPC methods."""
  __metaclass__ = abc.ABCMeta

  @abc.abstractmethod
  def service(self, request, context):
    """Services an RPC that accepts one value and produces a stream of values.

    Args:
      request: The single request value for the RPC.
      context: An RpcContext object.

    Yields:
      The values that comprise the response stream of the RPC.

    Raises:
      abandonment.Abandoned: If completing the response stream is not necessary
        because the RPC has been aborted.
    """
    raise NotImplementedError()


class InlineStreamInValueOutMethod(object):
  """A type for inline stream-request-unary-response RPC methods."""
  __metaclass__ = abc.ABCMeta

  @abc.abstractmethod
  def service(self, request_iterator, context):
    """Services an RPC that accepts a stream of values and produces one value.

    Args:
      request_iterator: An iterator that yields the request values of the RPC.
        Drawing values from this iterator may also raise exceptions.RpcError to
        indicate abortion of the RPC.
      context: An RpcContext object.

    Yields:
      The values that comprise the response stream of the RPC.

    Raises:
      abandonment.Abandoned: If no response is necessary because the RPC has
        been aborted.
      exceptions.RpcError: Implementations of this method must not deliberately
        raise exceptions.RpcError but may allow such errors raised by the
        request_iterator passed to them to propagate through their bodies
        uncaught.
    """
    raise NotImplementedError()


class InlineStreamInStreamOutMethod(object):
  """A type for inline stream-request-stream-response RPC methods."""
  __metaclass__ = abc.ABCMeta

  @abc.abstractmethod
  def service(self, request_iterator, context):
    """Services an RPC that accepts and produces streams of values.

    Args:
      request_iterator: An iterator that yields the request values of the RPC.
        Drawing values from this iterator may also raise exceptions.RpcError to
        indicate abortion of the RPC.
      context: An RpcContext object.

    Yields:
      The values that comprise the response stream of the RPC.

    Raises:
      abandonment.Abandoned: If completing the response stream is not necessary
        because the RPC has been aborted.
      exceptions.RpcError: Implementations of this method must not deliberately
        raise exceptions.RpcError but may allow such errors raised by the
        request_iterator passed to them to propagate through their bodies
        uncaught.
    """
    raise NotImplementedError()


class EventValueInValueOutMethod(object):
  """A type for event-driven unary-request-unary-response RPC methods."""
  __metaclass__ = abc.ABCMeta

  @abc.abstractmethod
  def service(self, request, response_callback, context):
    """Services an RPC that accepts one value and produces one value.

    Args:
      request: The single request value for the RPC.
      response_callback: A callback to be called to accept the response value of
        the RPC.
      context: An RpcContext object.

    Raises:
      abandonment.Abandoned: May or may not be raised when the RPC has been
        aborted.
    """
    raise NotImplementedError()


class EventValueInStreamOutMethod(object):
  """A type for event-driven unary-request-stream-response RPC methods."""
  __metaclass__ = abc.ABCMeta

  @abc.abstractmethod
  def service(self, request, response_consumer, context):
    """Services an RPC that accepts one value and produces a stream of values.

    Args:
      request: The single request value for the RPC.
      response_consumer: A stream.Consumer to be called to accept the response
        values of the RPC.
      context: An RpcContext object.

    Raises:
      abandonment.Abandoned: May or may not be raised when the RPC has been
        aborted.
    """
    raise NotImplementedError()


class EventStreamInValueOutMethod(object):
  """A type for event-driven stream-request-unary-response RPC methods."""
  __metaclass__ = abc.ABCMeta

  @abc.abstractmethod
  def service(self, response_callback, context):
    """Services an RPC that accepts a stream of values and produces one value.

    Args:
      response_callback: A callback to be called to accept the response value of
        the RPC.
      context: An RpcContext object.

    Returns:
      A stream.Consumer with which to accept the request values of the RPC. The
        consumer returned from this method may or may not be invoked to
        completion: in the case of RPC abortion, RPC Framework will simply stop
        passing values to this object. Implementations must not assume that this
        object will be called to completion of the request stream or even called
        at all.

    Raises:
      abandonment.Abandoned: May or may not be raised when the RPC has been
        aborted.
    """
    raise NotImplementedError()


class EventStreamInStreamOutMethod(object):
  """A type for event-driven stream-request-stream-response RPC methods."""
  __metaclass__ = abc.ABCMeta

  @abc.abstractmethod
  def service(self, response_consumer, context):
    """Services an RPC that accepts and produces streams of values.

    Args:
      response_consumer: A stream.Consumer to be called to accept the response
        values of the RPC.
      context: An RpcContext object.

    Returns:
      A stream.Consumer with which to accept the request values of the RPC. The
        consumer returned from this method may or may not be invoked to
        completion: in the case of RPC abortion, RPC Framework will simply stop
        passing values to this object. Implementations must not assume that this
        object will be called to completion of the request stream or even called
        at all.

    Raises:
      abandonment.Abandoned: May or may not be raised when the RPC has been
        aborted.
    """
    raise NotImplementedError()


class MultiMethod(object):
  """A general type able to service many RPC methods."""
  __metaclass__ = abc.ABCMeta

  @abc.abstractmethod
  def service(self, name, response_consumer, context):
    """Services an RPC.

    Args:
      name: The RPC method name.
      response_consumer: A stream.Consumer to be called to accept the response
        values of the RPC.
      context: An RpcContext object.

    Returns:
      A stream.Consumer with which to accept the request values of the RPC. The
        consumer returned from this method may or may not be invoked to
        completion: in the case of RPC abortion, RPC Framework will simply stop
        passing values to this object. Implementations must not assume that this
        object will be called to completion of the request stream or even called
        at all.

    Raises:
      abandonment.Abandoned: May or may not be raised when the RPC has been
        aborted.
      exceptions.NoSuchMethodError: If this MultiMethod does not recognize the
        given RPC method name and is not able to service the RPC.
    """
    raise NotImplementedError()


class Server(object):
  """Specification of a running server that services RPCs."""
  __metaclass__ = abc.ABCMeta


class Call(object):
  """Invocation-side representation of an RPC.

  Attributes:
    context: An RpcContext affording information about the RPC.
  """
  __metaclass__ = abc.ABCMeta

  @abc.abstractmethod
  def cancel(self):
    """Requests cancellation of the RPC."""
    raise NotImplementedError()


class Stub(object):
  """Affords RPC methods to callers."""
  __metaclass__ = abc.ABCMeta

  @abc.abstractmethod
  def blocking_value_in_value_out(self, name, request, timeout):
    """Invokes a unary-request-unary-response RPC method.

    This method blocks until either returning the response value of the RPC
    (in the event of RPC completion) or raising an exception (in the event of
    RPC abortion).

    Args:
      name: The RPC method name.
      request: The request value for the RPC.
      timeout: A duration of time in seconds to allow for the RPC.

    Returns:
      The response value for the RPC.

    Raises:
      exceptions.RpcError: Indicating that the RPC was aborted.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def future_value_in_value_out(self, name, request, timeout):
    """Invokes a unary-request-unary-response RPC method.

    Args:
      name: The RPC method name.
      request: The request value for the RPC.
      timeout: A duration of time in seconds to allow for the RPC.

    Returns:
      A future.Future representing the RPC. In the event of RPC completion, the
        returned Future will return an outcome indicating that the RPC returned
        the response value of the RPC. In the event of RPC abortion, the
        returned Future will return an outcome indicating that the RPC raised
        an exceptions.RpcError.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def inline_value_in_stream_out(self, name, request, timeout):
    """Invokes a unary-request-stream-response RPC method.

    Args:
      name: The RPC method name.
      request: The request value for the RPC.
      timeout: A duration of time in seconds to allow for the RPC.

    Returns:
      A CancellableIterator that yields the response values of the RPC and
        affords RPC cancellation. Drawing response values from the returned
        CancellableIterator may raise exceptions.RpcError indicating abortion of
        the RPC.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def blocking_stream_in_value_out(self, name, request_iterator, timeout):
    """Invokes a stream-request-unary-response RPC method.

    This method blocks until either returning the response value of the RPC
    (in the event of RPC completion) or raising an exception (in the event of
    RPC abortion).

    Args:
      name: The RPC method name.
      request_iterator: An iterator that yields the request values of the RPC.
      timeout: A duration of time in seconds to allow for the RPC.

    Returns:
      The response value for the RPC.

    Raises:
      exceptions.RpcError: Indicating that the RPC was aborted.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def future_stream_in_value_out(self, name, request_iterator, timeout):
    """Invokes a stream-request-unary-response RPC method.

    Args:
      name: The RPC method name.
      request_iterator: An iterator that yields the request values of the RPC.
      timeout: A duration of time in seconds to allow for the RPC.

    Returns:
      A future.Future representing the RPC. In the event of RPC completion, the
        returned Future will return an outcome indicating that the RPC returned
        the response value of the RPC. In the event of RPC abortion, the
        returned Future will return an outcome indicating that the RPC raised
        an exceptions.RpcError.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def inline_stream_in_stream_out(self, name, request_iterator, timeout):
    """Invokes a stream-request-stream-response RPC method.

    Args:
      name: The RPC method name.
      request_iterator: An iterator that yields the request values of the RPC.
      timeout: A duration of time in seconds to allow for the RPC.

    Returns:
      A CancellableIterator that yields the response values of the RPC and
        affords RPC cancellation. Drawing response values from the returned
        CancellableIterator may raise exceptions.RpcError indicating abortion of
        the RPC.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def event_value_in_value_out(
      self, name, request, response_callback, abortion_callback, timeout):
    """Event-driven invocation of a unary-request-unary-response RPC method.

    Args:
      name: The RPC method name.
      request: The request value for the RPC.
      response_callback: A callback to be called to accept the response value
        of the RPC.
      abortion_callback: A callback to be called and passed an Abortion value
        in the event of RPC abortion.
      timeout: A duration of time in seconds to allow for the RPC.

    Returns:
      A Call object for the RPC.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def event_value_in_stream_out(
      self, name, request, response_consumer, abortion_callback, timeout):
    """Event-driven invocation of a unary-request-stream-response RPC method.

    Args:
      name: The RPC method name.
      request: The request value for the RPC.
      response_consumer: A stream.Consumer to be called to accept the response
        values of the RPC.
      abortion_callback: A callback to be called and passed an Abortion value
        in the event of RPC abortion.
      timeout: A duration of time in seconds to allow for the RPC.

    Returns:
      A Call object for the RPC.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def event_stream_in_value_out(
      self, name, response_callback, abortion_callback, timeout):
    """Event-driven invocation of a unary-request-unary-response RPC method.

    Args:
      name: The RPC method name.
      response_callback: A callback to be called to accept the response value
        of the RPC.
      abortion_callback: A callback to be called and passed an Abortion value
        in the event of RPC abortion.
      timeout: A duration of time in seconds to allow for the RPC.

    Returns:
      A pair of a Call object for the RPC and a stream.Consumer to which the
        request values of the RPC should be passed.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def event_stream_in_stream_out(
      self, name, response_consumer, abortion_callback, timeout):
    """Event-driven invocation of a unary-request-stream-response RPC method.

    Args:
      name: The RPC method name.
      response_consumer: A stream.Consumer to be called to accept the response
        values of the RPC.
      abortion_callback: A callback to be called and passed an Abortion value
        in the event of RPC abortion.
      timeout: A duration of time in seconds to allow for the RPC.

    Returns:
      A pair of a Call object for the RPC and a stream.Consumer to which the
        request values of the RPC should be passed.
    """
    raise NotImplementedError()
