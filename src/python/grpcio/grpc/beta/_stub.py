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

"""Beta API stub implementation."""

import threading

from grpc._links import invocation
from grpc.framework.core import implementations as _core_implementations
from grpc.framework.crust import implementations as _crust_implementations
from grpc.framework.foundation import logging_pool
from grpc.framework.interfaces.links import utilities

_DEFAULT_POOL_SIZE = 6


class _AutoIntermediary(object):

  def __init__(self, up, down, delegate):
    self._lock = threading.Lock()
    self._up = up
    self._down = down
    self._in_context = False
    self._delegate = delegate

  def __getattr__(self, attr):
    with self._lock:
      if self._delegate is None:
        raise AttributeError('No useful attributes out of context!')
      else:
        return getattr(self._delegate, attr)

  def __enter__(self):
    with self._lock:
      if self._in_context:
        raise ValueError('Already in context!')
      elif self._delegate is None:
        self._delegate = self._up()
      self._in_context = True
      return self

  def __exit__(self, exc_type, exc_val, exc_tb):
    with self._lock:
      if not self._in_context:
        raise ValueError('Not in context!')
      self._down()
      self._in_context = False
      self._delegate = None
      return False

  def __del__(self):
    with self._lock:
      if self._delegate is not None:
        self._down()
        self._delegate = None


class _StubAssemblyManager(object):

  def __init__(
      self, thread_pool, thread_pool_size, end_link, grpc_link, stub_creator):
    self._thread_pool = thread_pool
    self._pool_size = thread_pool_size
    self._end_link = end_link
    self._grpc_link = grpc_link
    self._stub_creator = stub_creator
    self._own_pool = None

  def up(self):
    if self._thread_pool is None:
      self._own_pool = logging_pool.pool(
          _DEFAULT_POOL_SIZE if self._pool_size is None else self._pool_size)
      assembly_pool = self._own_pool
    else:
      assembly_pool = self._thread_pool
    self._end_link.join_link(self._grpc_link)
    self._grpc_link.join_link(self._end_link)
    self._end_link.start()
    self._grpc_link.start()
    return self._stub_creator(self._end_link, assembly_pool)

  def down(self):
    self._end_link.stop(0).wait()
    self._grpc_link.stop()
    self._end_link.join_link(utilities.NULL_LINK)
    self._grpc_link.join_link(utilities.NULL_LINK)
    if self._own_pool is not None:
      self._own_pool.shutdown(wait=True)
      self._own_pool = None


def _assemble(
    channel, host, metadata_transformer, request_serializers,
    response_deserializers, thread_pool, thread_pool_size, stub_creator):
  end_link = _core_implementations.invocation_end_link()
  grpc_link = invocation.invocation_link(
      channel, host, metadata_transformer, request_serializers,
      response_deserializers)
  stub_assembly_manager = _StubAssemblyManager(
      thread_pool, thread_pool_size, end_link, grpc_link, stub_creator)
  stub = stub_assembly_manager.up()
  return _AutoIntermediary(
      stub_assembly_manager.up, stub_assembly_manager.down, stub)


def _dynamic_stub_creator(service, cardinalities):
  def create_dynamic_stub(end_link, invocation_pool):
    return _crust_implementations.dynamic_stub(
        end_link, service, cardinalities, invocation_pool)
  return create_dynamic_stub


def generic_stub(
    channel, host, metadata_transformer, request_serializers,
    response_deserializers, thread_pool, thread_pool_size):
  return _assemble(
      channel, host, metadata_transformer, request_serializers,
      response_deserializers, thread_pool, thread_pool_size,
      _crust_implementations.generic_stub)


def dynamic_stub(
    channel, host, service, cardinalities, metadata_transformer,
    request_serializers, response_deserializers, thread_pool,
    thread_pool_size):
  return _assemble(
      channel, host, metadata_transformer, request_serializers,
      response_deserializers, thread_pool, thread_pool_size,
      _dynamic_stub_creator(service, cardinalities))
