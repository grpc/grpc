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

"""Entry points into the Face layer of RPC Framework."""

from grpc.framework.common import cardinality
from grpc.framework.common import style
from grpc.framework.base import exceptions as _base_exceptions
from grpc.framework.base import interfaces as base_interfaces
from grpc.framework.face import _calls
from grpc.framework.face import _service
from grpc.framework.face import exceptions
from grpc.framework.face import interfaces


class _BaseServicer(base_interfaces.Servicer):

  def __init__(self, methods, multi_method):
    self._methods = methods
    self._multi_method = multi_method

  def service(self, name, context, output_consumer):
    method = self._methods.get(name, None)
    if method is not None:
      return method(output_consumer, context)
    elif self._multi_method is not None:
      try:
        return self._multi_method.service(name, output_consumer, context)
      except exceptions.NoSuchMethodError:
        raise _base_exceptions.NoSuchMethodError()
    else:
      raise _base_exceptions.NoSuchMethodError()


class _UnaryUnaryMultiCallable(interfaces.UnaryUnaryMultiCallable):

  def __init__(self, front, name):
    self._front = front
    self._name = name

  def __call__(self, request, timeout):
    return _calls.blocking_value_in_value_out(
        self._front, self._name, request, timeout, 'unused trace ID')

  def future(self, request, timeout):
    return _calls.future_value_in_value_out(
        self._front, self._name, request, timeout, 'unused trace ID')

  def event(self, request, response_callback, abortion_callback, timeout):
    return _calls.event_value_in_value_out(
        self._front, self._name, request, response_callback, abortion_callback,
        timeout, 'unused trace ID')


class _UnaryStreamMultiCallable(interfaces.UnaryStreamMultiCallable):

  def __init__(self, front, name):
    self._front = front
    self._name = name

  def __call__(self, request, timeout):
    return _calls.inline_value_in_stream_out(
        self._front, self._name, request, timeout, 'unused trace ID')

  def event(self, request, response_consumer, abortion_callback, timeout):
    return _calls.event_value_in_stream_out(
        self._front, self._name, request, response_consumer, abortion_callback,
        timeout, 'unused trace ID')


class _StreamUnaryMultiCallable(interfaces.StreamUnaryMultiCallable):

  def __init__(self, front, name, pool):
    self._front = front
    self._name = name
    self._pool = pool

  def __call__(self, request_iterator, timeout):
    return _calls.blocking_stream_in_value_out(
        self._front, self._name, request_iterator, timeout, 'unused trace ID')

  def future(self, request_iterator, timeout):
    return _calls.future_stream_in_value_out(
        self._front, self._name, request_iterator, timeout, 'unused trace ID',
        self._pool)

  def event(self, response_callback, abortion_callback, timeout):
    return _calls.event_stream_in_value_out(
        self._front, self._name, response_callback, abortion_callback, timeout,
        'unused trace ID')


class _StreamStreamMultiCallable(interfaces.StreamStreamMultiCallable):

  def __init__(self, front, name, pool):
    self._front = front
    self._name = name
    self._pool = pool

  def __call__(self, request_iterator, timeout):
    return _calls.inline_stream_in_stream_out(
        self._front, self._name, request_iterator, timeout, 'unused trace ID',
        self._pool)

  def event(self, response_consumer, abortion_callback, timeout):
    return _calls.event_stream_in_stream_out(
        self._front, self._name, response_consumer, abortion_callback, timeout,
        'unused trace ID')


class _GenericStub(interfaces.GenericStub):
  """An interfaces.GenericStub implementation."""

  def __init__(self, front, pool):
    self._front = front
    self._pool = pool

  def blocking_value_in_value_out(self, name, request, timeout):
    return _calls.blocking_value_in_value_out(
        self._front, name, request, timeout, 'unused trace ID')

  def future_value_in_value_out(self, name, request, timeout):
    return _calls.future_value_in_value_out(
        self._front, name, request, timeout, 'unused trace ID')

  def inline_value_in_stream_out(self, name, request, timeout):
    return _calls.inline_value_in_stream_out(
        self._front, name, request, timeout, 'unused trace ID')

  def blocking_stream_in_value_out(self, name, request_iterator, timeout):
    return _calls.blocking_stream_in_value_out(
        self._front, name, request_iterator, timeout, 'unused trace ID')

  def future_stream_in_value_out(self, name, request_iterator, timeout):
    return _calls.future_stream_in_value_out(
        self._front, name, request_iterator, timeout, 'unused trace ID',
        self._pool)

  def inline_stream_in_stream_out(self, name, request_iterator, timeout):
    return _calls.inline_stream_in_stream_out(
        self._front, name, request_iterator, timeout, 'unused trace ID',
        self._pool)

  def event_value_in_value_out(
      self, name, request, response_callback, abortion_callback, timeout):
    return _calls.event_value_in_value_out(
        self._front, name, request, response_callback, abortion_callback,
        timeout, 'unused trace ID')

  def event_value_in_stream_out(
      self, name, request, response_consumer, abortion_callback, timeout):
    return _calls.event_value_in_stream_out(
        self._front, name, request, response_consumer, abortion_callback,
        timeout, 'unused trace ID')

  def event_stream_in_value_out(
      self, name, response_callback, abortion_callback, timeout):
    return _calls.event_stream_in_value_out(
        self._front, name, response_callback, abortion_callback, timeout,
        'unused trace ID')

  def event_stream_in_stream_out(
      self, name, response_consumer, abortion_callback, timeout):
    return _calls.event_stream_in_stream_out(
        self._front, name, response_consumer, abortion_callback, timeout,
        'unused trace ID')

  def unary_unary_multi_callable(self, name):
    return _UnaryUnaryMultiCallable(self._front, name)

  def unary_stream_multi_callable(self, name):
    return _UnaryStreamMultiCallable(self._front, name)

  def stream_unary_multi_callable(self, name):
    return _StreamUnaryMultiCallable(self._front, name, self._pool)

  def stream_stream_multi_callable(self, name):
    return _StreamStreamMultiCallable(self._front, name, self._pool)


class _DynamicStub(interfaces.DynamicStub):
  """An interfaces.DynamicStub implementation."""

  def __init__(self, cardinalities, front, pool):
    self._cardinalities = cardinalities
    self._front = front
    self._pool = pool

  def __getattr__(self, attr):
    method_cardinality = self._cardinalities.get(attr)
    if method_cardinality is cardinality.Cardinality.UNARY_UNARY:
      return _UnaryUnaryMultiCallable(self._front, attr)
    elif method_cardinality is cardinality.Cardinality.UNARY_STREAM:
      return _UnaryStreamMultiCallable(self._front, attr)
    elif method_cardinality is cardinality.Cardinality.STREAM_UNARY:
      return _StreamUnaryMultiCallable(self._front, attr, self._pool)
    elif method_cardinality is cardinality.Cardinality.STREAM_STREAM:
      return _StreamStreamMultiCallable(self._front, attr, self._pool)
    else:
      raise AttributeError('_DynamicStub object has no attribute "%s"!' % attr)


def _adapt_method_implementations(method_implementations, pool):
  adapted_implementations = {}
  for name, method_implementation in method_implementations.iteritems():
    if method_implementation.style is style.Service.INLINE:
      if method_implementation.cardinality is cardinality.Cardinality.UNARY_UNARY:
        adapted_implementations[name] = _service.adapt_inline_value_in_value_out(
            method_implementation.unary_unary_inline)
      elif method_implementation.cardinality is cardinality.Cardinality.UNARY_STREAM:
        adapted_implementations[name] = _service.adapt_inline_value_in_stream_out(
            method_implementation.unary_stream_inline)
      elif method_implementation.cardinality is cardinality.Cardinality.STREAM_UNARY:
        adapted_implementations[name] = _service.adapt_inline_stream_in_value_out(
            method_implementation.stream_unary_inline, pool)
      elif method_implementation.cardinality is cardinality.Cardinality.STREAM_STREAM:
        adapted_implementations[name] = _service.adapt_inline_stream_in_stream_out(
            method_implementation.stream_stream_inline, pool)
    elif method_implementation.style is style.Service.EVENT:
      if method_implementation.cardinality is cardinality.Cardinality.UNARY_UNARY:
        adapted_implementations[name] = _service.adapt_event_value_in_value_out(
            method_implementation.unary_unary_event)
      elif method_implementation.cardinality is cardinality.Cardinality.UNARY_STREAM:
        adapted_implementations[name] = _service.adapt_event_value_in_stream_out(
            method_implementation.unary_stream_event)
      elif method_implementation.cardinality is cardinality.Cardinality.STREAM_UNARY:
        adapted_implementations[name] = _service.adapt_event_stream_in_value_out(
            method_implementation.stream_unary_event)
      elif method_implementation.cardinality is cardinality.Cardinality.STREAM_STREAM:
        adapted_implementations[name] = _service.adapt_event_stream_in_stream_out(
            method_implementation.stream_stream_event)
  return adapted_implementations


def servicer(pool, method_implementations, multi_method_implementation):
  """Creates a base_interfaces.Servicer.

  It is guaranteed that any passed interfaces.MultiMethodImplementation will
  only be called to service an RPC if there is no
  interfaces.MethodImplementation for the RPC method in the passed
  method_implementations dictionary.

  Args:
    pool: A thread pool.
    method_implementations: A dictionary from RPC method name to
      interfaces.MethodImplementation object to be used to service the named
      RPC method.
    multi_method_implementation: An interfaces.MultiMethodImplementation to be
      used to service any RPCs not serviced by the
      interfaces.MethodImplementations given in the method_implementations
      dictionary, or None.

  Returns:
    A base_interfaces.Servicer that services RPCs via the given implementations.
  """
  adapted_implementations = _adapt_method_implementations(
      method_implementations, pool)
  return _BaseServicer(adapted_implementations, multi_method_implementation)


def generic_stub(front, pool):
  """Creates an interfaces.GenericStub.

  Args:
    front: A base_interfaces.Front.
    pool: A futures.ThreadPoolExecutor.

  Returns:
    An interfaces.GenericStub that performs RPCs via the given
      base_interfaces.Front.
  """
  return _GenericStub(front, pool)


def dynamic_stub(cardinalities, front, pool, prefix):
  """Creates an interfaces.DynamicStub.

  Args:
    cardinalities: A dict from RPC method name to cardinality.Cardinality
      value identifying the cardinality of every RPC method to be supported by
      the created interfaces.DynamicStub.
    front: A base_interfaces.Front.
    pool: A futures.ThreadPoolExecutor.
    prefix: A string to prepend when mapping requested attribute name to RPC
      method name during attribute access on the created
      interfaces.DynamicStub.

  Returns:
    An interfaces.DynamicStub that performs RPCs via the given
      base_interfaces.Front.
  """
  return _DynamicStub(cardinalities, front, pool)
