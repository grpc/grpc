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

  def __init__(self, delegate, on_deletion):
    self._delegate = delegate
    self._on_deletion = on_deletion

  def __getattr__(self, attr):
    return getattr(self._delegate, attr)

  def __enter__(self):
    return self

  def __exit__(self, exc_type, exc_val, exc_tb):
    return False

  def __del__(self):
    self._on_deletion()


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
  end_link.stop(24 * 60 * 60).wait()
  grpc_link.stop()
  end_link.join_link(utilities.NULL_LINK)
  grpc_link.join_link(utilities.NULL_LINK)
  if pool is not None:
    pool.shutdown(wait=True)


def _wrap_assembly(stub, end_link, grpc_link, assembly_pool):
  disassembly_thread = threading.Thread(
      target=_disassemble, args=(end_link, grpc_link, assembly_pool))
  return _AutoIntermediary(stub, disassembly_thread.start)


def generic_stub(
    channel, host, metadata_transformer, request_serializers,
    response_deserializers, thread_pool, thread_pool_size):
  end_link, grpc_link, invocation_pool, assembly_pool = _assemble(
      channel, host, metadata_transformer, request_serializers,
      response_deserializers, thread_pool, thread_pool_size)
  stub = _crust_implementations.generic_stub(end_link, invocation_pool)
  return _wrap_assembly(stub, end_link, grpc_link, assembly_pool)


def dynamic_stub(
    channel, host, service, cardinalities, metadata_transformer,
    request_serializers, response_deserializers, thread_pool,
    thread_pool_size):
  end_link, grpc_link, invocation_pool, assembly_pool = _assemble(
      channel, host, metadata_transformer, request_serializers,
      response_deserializers, thread_pool, thread_pool_size)
  stub = _crust_implementations.dynamic_stub(
      end_link, service, cardinalities, invocation_pool)
  return _wrap_assembly(stub, end_link, grpc_link, assembly_pool)
