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


def _assemble(
    channel, host, metadata_transformer, request_serializers,
    response_deserializers, thread_pool, thread_pool_size):
  end_link = _core_implementations.invocation_end_link()
  grpc_link = invocation.invocation_link(
      channel, host, metadata_transformer, request_serializers,
      response_deserializers)
  if thread_pool is None:
    invocation_pool = logging_pool.pool(
        _DEFAULT_POOL_SIZE if thread_pool_size is None else thread_pool_size)
    assembly_pool = invocation_pool
  else:
    invocation_pool = thread_pool
    assembly_pool = None
  end_link.join_link(grpc_link)
  grpc_link.join_link(end_link)
  end_link.start()
  grpc_link.start()
  return end_link, grpc_link, invocation_pool, assembly_pool


def _disassemble(end_link, grpc_link, pool):
  end_link.stop(0).wait()
  grpc_link.stop()
  end_link.join_link(utilities.NULL_LINK)
  grpc_link.join_link(utilities.NULL_LINK)
  if pool is not None:
    pool.shutdown(wait=True)


class _StubAssemblyManager(object):

  def __init__(
      self, channel, host, metadata_transformer, request_serializers,
      response_deserializers, thread_pool, thread_pool_size, end_link,
      grpc_link, assembly_pool, stub_creator):
    self._channel = channel
    self._host = host
    self._metadata_transformer = metadata_transformer
    self._request_serializers = request_serializers
    self._response_deserializers = response_deserializers
    self._thread_pool = thread_pool
    self._thread_pool_size = thread_pool_size
    self._end_link = end_link
    self._grpc_link = grpc_link
    self._assembly_pool = assembly_pool
    self._stub_creator = stub_creator

  def up(self):
    self._end_link, self._grpc_link, invocation_pool, self._assembly_pool = (
        _assemble(
            self._channel, self._host, self._metadata_transformer,
            self._request_serializers, self._response_deserializers,
            self._thread_pool, self._thread_pool_size))
    return self._stub_creator(self._end_link, invocation_pool)

  def down(self):
    _disassemble(self._end_link, self._grpc_link, self._assembly_pool)


def _dynamic_stub_creator(service, cardinalities):
  def create_dynamic_stub(end_link, invocation_pool):
    return _crust_implementations.dynamic_stub(
        end_link, service, cardinalities, invocation_pool)
  return create_dynamic_stub


def generic_stub(
    channel, host, metadata_transformer, request_serializers,
    response_deserializers, thread_pool, thread_pool_size):
  end_link, grpc_link, invocation_pool, assembly_pool = _assemble(
      channel, host, metadata_transformer, request_serializers,
      response_deserializers, thread_pool, thread_pool_size)
  stub_assembly = _StubAssemblyManager(
      channel, host, metadata_transformer, request_serializers,
      response_deserializers, thread_pool, thread_pool_size, end_link,
      grpc_link, assembly_pool, _crust_implementations.generic_stub)
  stub = _crust_implementations.generic_stub(end_link, invocation_pool)
  return _AutoIntermediary(stub_assembly.up, stub_assembly.down, stub)


def dynamic_stub(
    channel, host, service, cardinalities, metadata_transformer,
    request_serializers, response_deserializers, thread_pool,
    thread_pool_size):
  end_link, grpc_link, invocation_pool, assembly_pool = _assemble(
      channel, host, metadata_transformer, request_serializers,
      response_deserializers, thread_pool, thread_pool_size)
  stub_assembly = _StubAssemblyManager(
      channel, host, metadata_transformer, request_serializers,
      response_deserializers, thread_pool, thread_pool_size, end_link,
      grpc_link, assembly_pool, _dynamic_stub_creator(service, cardinalities))
  stub = _crust_implementations.dynamic_stub(
      end_link, service, cardinalities, invocation_pool)
  return _AutoIntermediary(stub_assembly.up, stub_assembly.down, stub)
