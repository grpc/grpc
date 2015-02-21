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

"""Implementations for assembling RPC framework values."""

import threading

from grpc.framework.assembly import interfaces
from grpc.framework.base import util as base_utilities
from grpc.framework.base.packets import implementations as tickets_implementations
from grpc.framework.base.packets import interfaces as tickets_interfaces
from grpc.framework.common import cardinality
from grpc.framework.common import style
from grpc.framework.face import implementations as face_implementations
from grpc.framework.face import interfaces as face_interfaces
from grpc.framework.face import utilities as face_utilities
from grpc.framework.foundation import activated
from grpc.framework.foundation import logging_pool

_ONE_DAY_IN_SECONDS = 60 * 60 * 24
_THREAD_POOL_SIZE = 100


class _FaceStub(object):

  def __init__(self, rear_link):
    self._rear_link = rear_link
    self._lock = threading.Lock()
    self._pool = None
    self._front = None
    self._under_stub = None

  def __enter__(self):
    with self._lock:
      self._pool = logging_pool.pool(_THREAD_POOL_SIZE)
      self._front = tickets_implementations.front(
          self._pool, self._pool, self._pool)
      self._rear_link.start()
      self._rear_link.join_fore_link(self._front)
      self._front.join_rear_link(self._rear_link)
      self._under_stub = face_implementations.stub(self._front, self._pool)

  def __exit__(self, exc_type, exc_val, exc_tb):
    with self._lock:
      self._under_stub = None
      self._rear_link.stop()
      base_utilities.wait_for_idle(self._front)
      self._front = None
      self._pool.shutdown(wait=True)
      self._pool = None
    return False

  def __getattr__(self, attr):
    with self._lock:
      if self._under_stub is None:
        raise ValueError('Called out of context!')
      else:
        return getattr(self._under_stub, attr)


def _behaviors(implementations, front, pool):
  behaviors = {}
  stub = face_implementations.stub(front, pool)
  for name, implementation in implementations.iteritems():
    if implementation.cardinality is cardinality.Cardinality.UNARY_UNARY:
      behaviors[name] = stub.unary_unary_sync_async(name)
    elif implementation.cardinality is cardinality.Cardinality.UNARY_STREAM:
      behaviors[name] = lambda request, context, bound_name=name: (
          stub.inline_value_in_stream_out(bound_name, request, context))
    elif implementation.cardinality is cardinality.Cardinality.STREAM_UNARY:
      behaviors[name] = stub.stream_unary_sync_async(name)
    elif implementation.cardinality is cardinality.Cardinality.STREAM_STREAM:
      behaviors[name] = lambda request_iterator, context, bound_name=name: (
          stub.inline_stream_in_stream_out(
              bound_name, request_iterator, context))
  return behaviors


class _DynamicInlineStub(object):

  def __init__(self, implementations, rear_link):
    self._implementations = implementations
    self._rear_link = rear_link
    self._lock = threading.Lock()
    self._pool = None
    self._front = None
    self._behaviors = None

  def __enter__(self):
    with self._lock:
      self._pool = logging_pool.pool(_THREAD_POOL_SIZE)
      self._front = tickets_implementations.front(
          self._pool, self._pool, self._pool)
      self._rear_link.start()
      self._rear_link.join_fore_link(self._front)
      self._front.join_rear_link(self._rear_link)
      self._behaviors = _behaviors(
          self._implementations, self._front, self._pool)
      return self

  def __exit__(self, exc_type, exc_val, exc_tb):
    with self._lock:
      self._behaviors = None
      self._rear_link.stop()
      base_utilities.wait_for_idle(self._front)
      self._front = None
      self._pool.shutdown(wait=True)
      self._pool = None
    return False

  def __getattr__(self, attr):
    with self._lock:
      behavior = self._behaviors.get(attr)
      if behavior is None:
        raise AttributeError(attr)
      else:
        return behavior


def _servicer(implementations, pool):
  inline_value_in_value_out_methods = {}
  inline_value_in_stream_out_methods = {}
  inline_stream_in_value_out_methods = {}
  inline_stream_in_stream_out_methods = {}
  event_value_in_value_out_methods = {}
  event_value_in_stream_out_methods = {}
  event_stream_in_value_out_methods = {}
  event_stream_in_stream_out_methods = {}

  for name, implementation in implementations.iteritems():
    if implementation.cardinality is cardinality.Cardinality.UNARY_UNARY:
      if implementation.style is style.Service.INLINE:
        inline_value_in_value_out_methods[name] = (
            face_utilities.inline_unary_unary_method(implementation.unary_unary_inline))
      elif implementation.style is style.Service.EVENT:
        event_value_in_value_out_methods[name] = (
            face_utilities.event_unary_unary_method(implementation.unary_unary_event))
    elif implementation.cardinality is cardinality.Cardinality.UNARY_STREAM:
      if implementation.style is style.Service.INLINE:
        inline_value_in_stream_out_methods[name] = (
            face_utilities.inline_unary_stream_method(implementation.unary_stream_inline))
      elif implementation.style is style.Service.EVENT:
        event_value_in_stream_out_methods[name] = (
            face_utilities.event_unary_stream_method(implementation.unary_stream_event))
    if implementation.cardinality is cardinality.Cardinality.STREAM_UNARY:
      if implementation.style is style.Service.INLINE:
        inline_stream_in_value_out_methods[name] = (
            face_utilities.inline_stream_unary_method(implementation.stream_unary_inline))
      elif implementation.style is style.Service.EVENT:
        event_stream_in_value_out_methods[name] = (
            face_utilities.event_stream_unary_method(implementation.stream_unary_event))
    elif implementation.cardinality is cardinality.Cardinality.STREAM_STREAM:
      if implementation.style is style.Service.INLINE:
        inline_stream_in_stream_out_methods[name] = (
            face_utilities.inline_stream_stream_method(implementation.stream_stream_inline))
      elif implementation.style is style.Service.EVENT:
        event_stream_in_stream_out_methods[name] = (
            face_utilities.event_stream_stream_method(implementation.stream_stream_event))

  return face_implementations.servicer(
      pool,
      inline_value_in_value_out_methods=inline_value_in_value_out_methods,
      inline_value_in_stream_out_methods=inline_value_in_stream_out_methods,
      inline_stream_in_value_out_methods=inline_stream_in_value_out_methods,
      inline_stream_in_stream_out_methods=inline_stream_in_stream_out_methods,
      event_value_in_value_out_methods=event_value_in_value_out_methods,
      event_value_in_stream_out_methods=event_value_in_stream_out_methods,
      event_stream_in_value_out_methods=event_stream_in_value_out_methods,
      event_stream_in_stream_out_methods=event_stream_in_stream_out_methods)


class _ServiceAssembly(activated.Activated):

  def __init__(self, implementations, fore_link):
    self._implementations = implementations
    self._fore_link = fore_link
    self._lock = threading.Lock()
    self._pool = None
    self._back = None

  def _start(self):
    with self._lock:
      self._pool = logging_pool.pool(_THREAD_POOL_SIZE)
      servicer = _servicer(self._implementations, self._pool)
      self._back = tickets_implementations.back(
          servicer, self._pool, self._pool, self._pool, _ONE_DAY_IN_SECONDS,
          _ONE_DAY_IN_SECONDS)
      self._fore_link.start()
      self._fore_link.join_rear_link(self._back)
      self._back.join_fore_link(self._fore_link)

  def _stop(self):
    with self._lock:
      self._fore_link.stop()
      base_utilities.wait_for_idle(self._back)
      self._back = None
      self._pool.shutdown(wait=True)
      self._pool = None

  def __enter__(self):
    self._start()
    return self

  def __exit__(self, exc_type, exc_val, exc_tb):
    self._stop()
    return False

  def start(self):
    return self._start()

  def stop(self):
    self._stop()


def assemble_face_stub(activated_rear_link):
  """Assembles a face_interfaces.Stub.

  The returned object is a context manager and may only be used in context to
  invoke RPCs.

  Args:
    activated_rear_link: An object that is both a tickets_interfaces.RearLink
      and an activated.Activated. The object should be in the inactive state
      when passed to this method.

  Returns:
    A face_interfaces.Stub on which, in context, RPCs can be invoked.
  """
  return _FaceStub(activated_rear_link)


def assemble_dynamic_inline_stub(implementations, activated_rear_link):
  """Assembles a stub with method names for attributes.

  The returned object is a context manager and may only be used in context to
  invoke RPCs.

  The returned object, when used in context, will respond to attribute access
  as follows: if the requested attribute is the name of a unary-unary RPC
  method, the value of the attribute will be a
  face_interfaces.UnaryUnarySyncAsync with which to invoke the RPC method. If
  the requested attribute is the name of a unary-stream RPC method, the value
  of the attribute will be a callable with the semantics of
  face_interfaces.Stub.inline_value_in_stream_out, minus the "name" parameter,
  with which to invoke the RPC method. If the requested attribute is the name
  of a stream-unary RPC method, the value of the attribute will be a
  face_interfaces.StreamUnarySyncAsync with which to invoke the RPC method. If
  the requested attribute is the name of a stream-stream RPC method, the value
  of the attribute will be a callable with the semantics of
  face_interfaces.Stub.inline_stream_in_stream_out, minus the "name" parameter,
  with which to invoke the RPC method.

  Args:
    implementations: A dictionary from RPC method name to
      interfaces.MethodImplementation.
    activated_rear_link: An object that is both a tickets_interfaces.RearLink
      and an activated.Activated. The object should be in the inactive state
      when passed to this method.

  Returns:
    A stub on which, in context, RPCs can be invoked.
  """
  return _DynamicInlineStub(implementations, activated_rear_link)


def assemble_service(implementations, activated_fore_link):
  """Assembles the service-side of the RPC Framework stack.

  Args:
    implementations: A dictionary from RPC method name to
      interfaces.MethodImplementation.
    activated_fore_link: An object that is both a tickets_interfaces.ForeLink
      and an activated.Activated. The object should be in the inactive state
      when passed to this method.

  Returns:
    An activated.Activated value encapsulating RPC service.
  """
  return _ServiceAssembly(implementations, activated_fore_link)
