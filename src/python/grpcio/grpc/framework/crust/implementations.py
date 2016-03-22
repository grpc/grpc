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

"""Entry points into the Crust layer of RPC Framework."""

from grpc.framework.common import cardinality
from grpc.framework.common import style
from grpc.framework.crust import _calls
from grpc.framework.crust import _service
from grpc.framework.interfaces.base import base
from grpc.framework.interfaces.face import face


class _BaseServicer(base.Servicer):

  def __init__(self, adapted_methods, adapted_multi_method):
    self._adapted_methods = adapted_methods
    self._adapted_multi_method = adapted_multi_method

  def service(self, group, method, context, output_operator):
    adapted_method = self._adapted_methods.get((group, method), None)
    if adapted_method is not None:
      return adapted_method(output_operator, context)
    elif self._adapted_multi_method is not None:
      try:
        return self._adapted_multi_method(
            group, method, output_operator, context)
      except face.NoSuchMethodError:
        raise base.NoSuchMethodError(None, None)
    else:
      raise base.NoSuchMethodError(None, None)


class _UnaryUnaryMultiCallable(face.UnaryUnaryMultiCallable):

  def __init__(self, end, group, method, pool):
    self._end = end
    self._group = group
    self._method = method
    self._pool = pool

  def __call__(
      self, request, timeout, metadata=None, with_call=False,
      protocol_options=None):
    return _calls.blocking_unary_unary(
        self._end, self._group, self._method, timeout, with_call,
        protocol_options, metadata, request)

  def future(self, request, timeout, metadata=None, protocol_options=None):
    return _calls.future_unary_unary(
        self._end, self._group, self._method, timeout, protocol_options,
        metadata, request)

  def event(
      self, request, receiver, abortion_callback, timeout,
      metadata=None, protocol_options=None):
    return _calls.event_unary_unary(
        self._end, self._group, self._method, timeout, protocol_options,
        metadata, request, receiver, abortion_callback, self._pool)


class _UnaryStreamMultiCallable(face.UnaryStreamMultiCallable):

  def __init__(self, end, group, method, pool):
    self._end = end
    self._group = group
    self._method = method
    self._pool = pool

  def __call__(self, request, timeout, metadata=None, protocol_options=None):
    return _calls.inline_unary_stream(
        self._end, self._group, self._method, timeout, protocol_options,
        metadata, request)

  def event(
      self, request, receiver, abortion_callback, timeout,
      metadata=None, protocol_options=None):
    return _calls.event_unary_stream(
        self._end, self._group, self._method, timeout, protocol_options,
        metadata, request, receiver, abortion_callback, self._pool)


class _StreamUnaryMultiCallable(face.StreamUnaryMultiCallable):

  def __init__(self, end, group, method, pool):
    self._end = end
    self._group = group
    self._method = method
    self._pool = pool

  def __call__(
      self, request_iterator, timeout, metadata=None,
      with_call=False, protocol_options=None):
    return _calls.blocking_stream_unary(
        self._end, self._group, self._method, timeout, with_call,
        protocol_options, metadata, request_iterator, self._pool)

  def future(
      self, request_iterator, timeout, metadata=None, protocol_options=None):
    return _calls.future_stream_unary(
        self._end, self._group, self._method, timeout, protocol_options,
        metadata, request_iterator, self._pool)

  def event(
      self, receiver, abortion_callback, timeout, metadata=None,
      protocol_options=None):
    return _calls.event_stream_unary(
        self._end, self._group, self._method, timeout, protocol_options,
        metadata, receiver, abortion_callback, self._pool)


class _StreamStreamMultiCallable(face.StreamStreamMultiCallable):

  def __init__(self, end, group, method, pool):
    self._end = end
    self._group = group
    self._method = method
    self._pool = pool

  def __call__(
      self, request_iterator, timeout, metadata=None, protocol_options=None):
    return _calls.inline_stream_stream(
        self._end, self._group, self._method, timeout, protocol_options,
        metadata, request_iterator, self._pool)

  def event(
      self, receiver, abortion_callback, timeout, metadata=None,
      protocol_options=None):
    return _calls.event_stream_stream(
        self._end, self._group, self._method, timeout, protocol_options,
        metadata, receiver, abortion_callback, self._pool)


class _GenericStub(face.GenericStub):
  """An face.GenericStub implementation."""

  def __init__(self, end, pool):
    self._end = end
    self._pool = pool

  def blocking_unary_unary(
      self, group, method, request, timeout, metadata=None,
      with_call=None, protocol_options=None):
    return _calls.blocking_unary_unary(
        self._end, group, method, timeout, with_call, protocol_options,
        metadata, request)

  def future_unary_unary(
      self, group, method, request, timeout, metadata=None,
      protocol_options=None):
    return _calls.future_unary_unary(
        self._end, group, method, timeout, protocol_options, metadata, request)

  def inline_unary_stream(
      self, group, method, request, timeout, metadata=None,
      protocol_options=None):
    return _calls.inline_unary_stream(
        self._end, group, method, timeout, protocol_options, metadata, request)

  def blocking_stream_unary(
      self, group, method, request_iterator, timeout, metadata=None,
      with_call=None, protocol_options=None):
    return _calls.blocking_stream_unary(
        self._end, group, method, timeout, with_call, protocol_options,
        metadata, request_iterator, self._pool)

  def future_stream_unary(
      self, group, method, request_iterator, timeout, metadata=None,
      protocol_options=None):
    return _calls.future_stream_unary(
        self._end, group, method, timeout, protocol_options, metadata,
        request_iterator, self._pool)

  def inline_stream_stream(
      self, group, method, request_iterator, timeout, metadata=None,
      protocol_options=None):
    return _calls.inline_stream_stream(
        self._end, group, method, timeout, protocol_options, metadata,
        request_iterator, self._pool)

  def event_unary_unary(
      self, group, method, request, receiver, abortion_callback, timeout,
      metadata=None, protocol_options=None):
    return _calls.event_unary_unary(
        self._end, group, method, timeout, protocol_options, metadata, request,
        receiver, abortion_callback, self._pool)

  def event_unary_stream(
      self, group, method, request, receiver, abortion_callback, timeout,
      metadata=None, protocol_options=None):
    return _calls.event_unary_stream(
        self._end, group, method, timeout, protocol_options, metadata, request,
        receiver, abortion_callback, self._pool)

  def event_stream_unary(
      self, group, method, receiver, abortion_callback, timeout,
      metadata=None, protocol_options=None):
    return _calls.event_stream_unary(
        self._end, group, method, timeout, protocol_options, metadata, receiver,
        abortion_callback, self._pool)

  def event_stream_stream(
      self, group, method, receiver, abortion_callback, timeout,
      metadata=None, protocol_options=None):
    return _calls.event_stream_stream(
        self._end, group, method, timeout, protocol_options, metadata, receiver,
        abortion_callback, self._pool)

  def unary_unary(self, group, method):
    return _UnaryUnaryMultiCallable(self._end, group, method, self._pool)

  def unary_stream(self, group, method):
    return _UnaryStreamMultiCallable(self._end, group, method, self._pool)

  def stream_unary(self, group, method):
    return _StreamUnaryMultiCallable(self._end, group, method, self._pool)

  def stream_stream(self, group, method):
    return _StreamStreamMultiCallable(self._end, group, method, self._pool)


class _DynamicStub(face.DynamicStub):
  """An face.DynamicStub implementation."""

  def __init__(self, end, group, cardinalities, pool):
    self._end = end
    self._group = group
    self._cardinalities = cardinalities
    self._pool = pool

  def __getattr__(self, attr):
    method_cardinality = self._cardinalities.get(attr)
    if method_cardinality is cardinality.Cardinality.UNARY_UNARY:
      return _UnaryUnaryMultiCallable(self._end, self._group, attr, self._pool)
    elif method_cardinality is cardinality.Cardinality.UNARY_STREAM:
      return _UnaryStreamMultiCallable(self._end, self._group, attr, self._pool)
    elif method_cardinality is cardinality.Cardinality.STREAM_UNARY:
      return _StreamUnaryMultiCallable(self._end, self._group, attr, self._pool)
    elif method_cardinality is cardinality.Cardinality.STREAM_STREAM:
      return _StreamStreamMultiCallable(
          self._end, self._group, attr, self._pool)
    else:
      raise AttributeError('_DynamicStub object has no attribute "%s"!' % attr)


def _adapt_method_implementations(method_implementations, pool):
  adapted_implementations = {}
  for name, method_implementation in method_implementations.iteritems():
    if method_implementation.style is style.Service.INLINE:
      if method_implementation.cardinality is cardinality.Cardinality.UNARY_UNARY:
        adapted_implementations[name] = _service.adapt_inline_unary_unary(
            method_implementation.unary_unary_inline, pool)
      elif method_implementation.cardinality is cardinality.Cardinality.UNARY_STREAM:
        adapted_implementations[name] = _service.adapt_inline_unary_stream(
            method_implementation.unary_stream_inline, pool)
      elif method_implementation.cardinality is cardinality.Cardinality.STREAM_UNARY:
        adapted_implementations[name] = _service.adapt_inline_stream_unary(
            method_implementation.stream_unary_inline, pool)
      elif method_implementation.cardinality is cardinality.Cardinality.STREAM_STREAM:
        adapted_implementations[name] = _service.adapt_inline_stream_stream(
            method_implementation.stream_stream_inline, pool)
    elif method_implementation.style is style.Service.EVENT:
      if method_implementation.cardinality is cardinality.Cardinality.UNARY_UNARY:
        adapted_implementations[name] = _service.adapt_event_unary_unary(
            method_implementation.unary_unary_event, pool)
      elif method_implementation.cardinality is cardinality.Cardinality.UNARY_STREAM:
        adapted_implementations[name] = _service.adapt_event_unary_stream(
            method_implementation.unary_stream_event, pool)
      elif method_implementation.cardinality is cardinality.Cardinality.STREAM_UNARY:
        adapted_implementations[name] = _service.adapt_event_stream_unary(
            method_implementation.stream_unary_event, pool)
      elif method_implementation.cardinality is cardinality.Cardinality.STREAM_STREAM:
        adapted_implementations[name] = _service.adapt_event_stream_stream(
            method_implementation.stream_stream_event, pool)
  return adapted_implementations


def servicer(method_implementations, multi_method_implementation, pool):
  """Creates a base.Servicer.

  It is guaranteed that any passed face.MultiMethodImplementation will
  only be called to service an RPC if there is no
  face.MethodImplementation for the RPC method in the passed
  method_implementations dictionary.

  Args:
    method_implementations: A dictionary from RPC method name to
      face.MethodImplementation object to be used to service the named
      RPC method.
    multi_method_implementation: An face.MultiMethodImplementation to be
      used to service any RPCs not serviced by the
      face.MethodImplementations given in the method_implementations
      dictionary, or None.
    pool: A thread pool.

  Returns:
    A base.Servicer that services RPCs via the given implementations.
  """
  adapted_implementations = _adapt_method_implementations(
      method_implementations, pool)
  if multi_method_implementation is None:
    adapted_multi_method_implementation = None
  else:
    adapted_multi_method_implementation = _service.adapt_multi_method(
        multi_method_implementation, pool)
  return _BaseServicer(
      adapted_implementations, adapted_multi_method_implementation)


def generic_stub(end, pool):
  """Creates an face.GenericStub.

  Args:
    end: A base.End.
    pool: A futures.ThreadPoolExecutor.

  Returns:
    A face.GenericStub that performs RPCs via the given base.End.
  """
  return _GenericStub(end, pool)


def dynamic_stub(end, group, cardinalities, pool):
  """Creates an face.DynamicStub.

  Args:
    end: A base.End.
    group: The group identifier for all RPCs to be made with the created
      face.DynamicStub.
    cardinalities: A dict from method identifier to cardinality.Cardinality
      value identifying the cardinality of every RPC method to be supported by
      the created face.DynamicStub.
    pool: A futures.ThreadPoolExecutor.

  Returns:
    A face.DynamicStub that performs RPCs via the given base.End.
  """
  return _DynamicStub(end, group, cardinalities, pool)
