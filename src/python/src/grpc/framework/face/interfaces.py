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

# cardinality, style, exceptions, abandonment, future, and stream are
# referenced from specification in this module.
from grpc.framework.common import cardinality  # pylint: disable=unused-import
from grpc.framework.common import style  # pylint: disable=unused-import
from grpc.framework.face import exceptions  # pylint: disable=unused-import
from grpc.framework.foundation import abandonment  # pylint: disable=unused-import
from grpc.framework.foundation import future  # pylint: disable=unused-import
from grpc.framework.foundation import stream  # pylint: disable=unused-import


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


class UnaryUnaryMultiCallable(object):
  """Affords invoking a unary-unary RPC in any call style."""
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
  def future(self, request, timeout):
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

  @abc.abstractmethod
  def event(self, request, response_callback, abortion_callback, timeout):
    """Asynchronously invokes the underlying RPC.

    Args:
      request: The request value for the RPC.
      response_callback: A callback to be called to accept the restponse value
        of the RPC.
      abortion_callback: A callback to be called and passed an Abortion value
        in the event of RPC abortion.
      timeout: A duration of time in seconds to allow for the RPC.

    Returns:
      A Call object for the RPC.
    """
    raise NotImplementedError()


class UnaryStreamMultiCallable(object):
  """Affords invoking a unary-stream RPC in any call style."""
  __metaclass__ = abc.ABCMeta

  @abc.abstractmethod
  def __call__(self, request, timeout):
    """Synchronously invokes the underlying RPC.

    Args:
      request: The request value for the RPC.
      timeout: A duration of time in seconds to allow for the RPC.

    Returns:
      A CancellableIterator that yields the response values of the RPC and
        affords RPC cancellation. Drawing response values from the returned
        CancellableIterator may raise exceptions.RpcError indicating abortion
        of the RPC.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def event(self, request, response_consumer, abortion_callback, timeout):
    """Asynchronously invokes the underlying RPC.

    Args:
      request: The request value for the RPC.
      response_consumer: A stream.Consumer to be called to accept the restponse
        values of the RPC.
      abortion_callback: A callback to be called and passed an Abortion value
        in the event of RPC abortion.
      timeout: A duration of time in seconds to allow for the RPC.

    Returns:
      A Call object for the RPC.
    """
    raise NotImplementedError()


class StreamUnaryMultiCallable(object):
  """Affords invoking a stream-unary RPC in any call style."""
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
  def future(self, request_iterator, timeout):
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

  @abc.abstractmethod
  def event(self, response_callback, abortion_callback, timeout):
    """Asynchronously invokes the underlying RPC.

    Args:
      request: The request value for the RPC.
      response_callback: A callback to be called to accept the restponse value
        of the RPC.
      abortion_callback: A callback to be called and passed an Abortion value
        in the event of RPC abortion.
      timeout: A duration of time in seconds to allow for the RPC.

    Returns:
      A pair of a Call object for the RPC and a stream.Consumer to which the
        request values of the RPC should be passed.
    """
    raise NotImplementedError()


class StreamStreamMultiCallable(object):
  """Affords invoking a stream-stream RPC in any call style."""
  __metaclass__ = abc.ABCMeta

  @abc.abstractmethod
  def __call__(self, request_iterator, timeout):
    """Synchronously invokes the underlying RPC.

    Args:
      request_iterator: An iterator that yields request values for the RPC.
      timeout: A duration of time in seconds to allow for the RPC.

    Returns:
      A CancellableIterator that yields the response values of the RPC and
        affords RPC cancellation. Drawing response values from the returned
        CancellableIterator may raise exceptions.RpcError indicating abortion
        of the RPC.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def event(self, response_consumer, abortion_callback, timeout):
    """Asynchronously invokes the underlying RPC.

l    Args:
      response_consumer: A stream.Consumer to be called to accept the restponse
        values of the RPC.
      abortion_callback: A callback to be called and passed an Abortion value
        in the event of RPC abortion.
      timeout: A duration of time in seconds to allow for the RPC.

    Returns:
      A pair of a Call object for the RPC and a stream.Consumer to which the
        request values of the RPC should be passed.
    """
    raise NotImplementedError()


class MethodImplementation(object):
  """A sum type that describes an RPC method implementation.

  Attributes:
    cardinality: A cardinality.Cardinality value.
    style: A style.Service value.
    unary_unary_inline: The implementation of the RPC method as a callable
      value that takes a request value and an RpcContext object and returns a
      response value. Only non-None if cardinality is
      cardinality.Cardinality.UNARY_UNARY and style is style.Service.INLINE.
    unary_stream_inline: The implementation of the RPC method as a callable
      value that takes a request value and an RpcContext object and returns an
      iterator of response values. Only non-None if cardinality is
      cardinality.Cardinality.UNARY_STREAM and style is style.Service.INLINE.
    stream_unary_inline: The implementation of the RPC method as a callable
      value that takes an iterator of request values and an RpcContext object
      and returns a response value. Only non-None if cardinality is
      cardinality.Cardinality.STREAM_UNARY and style is style.Service.INLINE.
    stream_stream_inline: The implementation of the RPC method as a callable
      value that takes an iterator of request values and an RpcContext object
      and returns an iterator of response values. Only non-None if cardinality
      is cardinality.Cardinality.STREAM_STREAM and style is
      style.Service.INLINE.
    unary_unary_event: The implementation of the RPC method as a callable value
      that takes a request value, a response callback to which to pass the
      response value of the RPC, and an RpcContext. Only non-None if
      cardinality is cardinality.Cardinality.UNARY_UNARY and style is
      style.Service.EVENT.
    unary_stream_event: The implementation of the RPC method as a callable
      value that takes a request value, a stream.Consumer to which to pass the
      the response values of the RPC, and an RpcContext. Only non-None if
      cardinality is cardinality.Cardinality.UNARY_STREAM and style is
      style.Service.EVENT.
    stream_unary_event: The implementation of the RPC method as a callable
      value that takes a response callback to which to pass the response value
      of the RPC and an RpcContext and returns a stream.Consumer to which the
      request values of the RPC should be passed. Only non-None if cardinality
      is cardinality.Cardinality.STREAM_UNARY and style is style.Service.EVENT.
    stream_stream_event: The implementation of the RPC method as a callable
      value that takes a stream.Consumer to which to pass the response values
      of the RPC and an RpcContext and returns a stream.Consumer to which the
      request values of the RPC should be passed. Only non-None if cardinality
      is cardinality.Cardinality.STREAM_STREAM and style is
      style.Service.EVENT.
  """
  __metaclass__ = abc.ABCMeta


class MultiMethodImplementation(object):
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


class GenericStub(object):
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

  @abc.abstractmethod
  def unary_unary_multi_callable(self, name):
    """Creates a UnaryUnaryMultiCallable for a unary-unary RPC method.

    Args:
      name: The RPC method name.

    Returns:
      A UnaryUnaryMultiCallable value for the named unary-unary RPC method.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def unary_stream_multi_callable(self, name):
    """Creates a UnaryStreamMultiCallable for a unary-stream RPC method.

    Args:
      name: The RPC method name.

    Returns:
      A UnaryStreamMultiCallable value for the name unary-stream RPC method.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def stream_unary_multi_callable(self, name):
    """Creates a StreamUnaryMultiCallable for a stream-unary RPC method.

    Args:
      name: The RPC method name.

    Returns:
      A StreamUnaryMultiCallable value for the named stream-unary RPC method.
    """
    raise NotImplementedError()

  @abc.abstractmethod
  def stream_stream_multi_callable(self, name):
    """Creates a StreamStreamMultiCallable for a stream-stream RPC method.

    Args:
      name: The RPC method name.

    Returns:
      A StreamStreamMultiCallable value for the named stream-stream RPC method.
    """
    raise NotImplementedError()


class DynamicStub(object):
  """A stub with RPC-method-bound multi-callable attributes.

  Instances of this type responsd to attribute access as follows: if the
  requested attribute is the name of a unary-unary RPC method, the value of the
  attribute will be a UnaryUnaryMultiCallable with which to invoke the RPC
  method; if the requested attribute is the name of a unary-stream RPC method,
  the value of the attribute will be a UnaryStreamMultiCallable with which to
  invoke the RPC method; if the requested attribute is the name of a
  stream-unary RPC method, the value of the attribute will be a
  StreamUnaryMultiCallable with which to invoke the RPC method; and if the
  requested attribute is the name of a stream-stream RPC method, the value of
  the attribute will be a StreamStreamMultiCallable with which to invoke the
  RPC method.
  """
  __metaclass__ = abc.ABCMeta
