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


class _UnaryUnarySyncAsync(interfaces.UnaryUnarySyncAsync):

  def __init__(self, front, name):
    self._front = front
    self._name = name

  def __call__(self, request, timeout):
    return _calls.blocking_value_in_value_out(
        self._front, self._name, request, timeout, 'unused trace ID')

  def async(self, request, timeout):
    return _calls.future_value_in_value_out(
        self._front, self._name, request, timeout, 'unused trace ID')


class _StreamUnarySyncAsync(interfaces.StreamUnarySyncAsync):

  def __init__(self, front, name, pool):
    self._front = front
    self._name = name
    self._pool = pool

  def __call__(self, request_iterator, timeout):
    return _calls.blocking_stream_in_value_out(
        self._front, self._name, request_iterator, timeout, 'unused trace ID')

  def async(self, request_iterator, timeout):
    return _calls.future_stream_in_value_out(
        self._front, self._name, request_iterator, timeout, 'unused trace ID',
        self._pool)


class _Server(interfaces.Server):
  """An interfaces.Server implementation."""


class _Stub(interfaces.Stub):
  """An interfaces.Stub implementation."""

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

  def unary_unary_sync_async(self, name):
    return _UnaryUnarySyncAsync(self._front, name)

  def stream_unary_sync_async(self, name):
    return _StreamUnarySyncAsync(self._front, name, self._pool)


def _aggregate_methods(
    pool,
    inline_value_in_value_out_methods,
    inline_value_in_stream_out_methods,
    inline_stream_in_value_out_methods,
    inline_stream_in_stream_out_methods,
    event_value_in_value_out_methods,
    event_value_in_stream_out_methods,
    event_stream_in_value_out_methods,
    event_stream_in_stream_out_methods):
  """Aggregates methods coded in according to different interfaces."""
  methods = {}

  def adapt_unpooled_methods(adapted_methods, unadapted_methods, adaptation):
    if unadapted_methods is not None:
      for name, unadapted_method in unadapted_methods.iteritems():
        adapted_methods[name] = adaptation(unadapted_method)

  def adapt_pooled_methods(adapted_methods, unadapted_methods, adaptation):
    if unadapted_methods is not None:
      for name, unadapted_method in unadapted_methods.iteritems():
        adapted_methods[name] = adaptation(unadapted_method, pool)

  adapt_unpooled_methods(
      methods, inline_value_in_value_out_methods,
      _service.adapt_inline_value_in_value_out)
  adapt_unpooled_methods(
      methods, inline_value_in_stream_out_methods,
      _service.adapt_inline_value_in_stream_out)
  adapt_pooled_methods(
      methods, inline_stream_in_value_out_methods,
      _service.adapt_inline_stream_in_value_out)
  adapt_pooled_methods(
      methods, inline_stream_in_stream_out_methods,
      _service.adapt_inline_stream_in_stream_out)
  adapt_unpooled_methods(
      methods, event_value_in_value_out_methods,
      _service.adapt_event_value_in_value_out)
  adapt_unpooled_methods(
      methods, event_value_in_stream_out_methods,
      _service.adapt_event_value_in_stream_out)
  adapt_unpooled_methods(
      methods, event_stream_in_value_out_methods,
      _service.adapt_event_stream_in_value_out)
  adapt_unpooled_methods(
      methods, event_stream_in_stream_out_methods,
      _service.adapt_event_stream_in_stream_out)

  return methods


def servicer(
    pool,
    inline_value_in_value_out_methods=None,
    inline_value_in_stream_out_methods=None,
    inline_stream_in_value_out_methods=None,
    inline_stream_in_stream_out_methods=None,
    event_value_in_value_out_methods=None,
    event_value_in_stream_out_methods=None,
    event_stream_in_value_out_methods=None,
    event_stream_in_stream_out_methods=None,
    multi_method=None):
  """Creates a base_interfaces.Servicer.

  The key sets of the passed dictionaries must be disjoint. It is guaranteed
  that any passed MultiMethod implementation will only be called to service an
  RPC if the RPC method name is not present in the key sets of the passed
  dictionaries.

  Args:
    pool: A thread pool.
    inline_value_in_value_out_methods: A dictionary mapping method names to
      interfaces.InlineValueInValueOutMethod implementations.
    inline_value_in_stream_out_methods: A dictionary mapping method names to
      interfaces.InlineValueInStreamOutMethod implementations.
    inline_stream_in_value_out_methods: A dictionary mapping method names to
      interfaces.InlineStreamInValueOutMethod implementations.
    inline_stream_in_stream_out_methods: A dictionary mapping method names to
      interfaces.InlineStreamInStreamOutMethod implementations.
    event_value_in_value_out_methods: A dictionary mapping method names to
      interfaces.EventValueInValueOutMethod implementations.
    event_value_in_stream_out_methods: A dictionary mapping method names to
      interfaces.EventValueInStreamOutMethod implementations.
    event_stream_in_value_out_methods: A dictionary mapping method names to
      interfaces.EventStreamInValueOutMethod implementations.
    event_stream_in_stream_out_methods: A dictionary mapping method names to
      interfaces.EventStreamInStreamOutMethod implementations.
    multi_method: An implementation of interfaces.MultiMethod.

  Returns:
    A base_interfaces.Servicer that services RPCs via the given implementations.
  """
  methods = _aggregate_methods(
      pool,
      inline_value_in_value_out_methods,
      inline_value_in_stream_out_methods,
      inline_stream_in_value_out_methods,
      inline_stream_in_stream_out_methods,
      event_value_in_value_out_methods,
      event_value_in_stream_out_methods,
      event_stream_in_value_out_methods,
      event_stream_in_stream_out_methods)

  return _BaseServicer(methods, multi_method)


def server():
  """Creates an interfaces.Server.

  Returns:
    An interfaces.Server.
  """
  return _Server()


def stub(front, pool):
  """Creates an interfaces.Stub.

  Args:
    front: A base_interfaces.Front.
    pool: A futures.ThreadPoolExecutor.

  Returns:
    An interfaces.Stub that performs RPCs via the given base_interfaces.Front.
  """
  return _Stub(front, pool)
