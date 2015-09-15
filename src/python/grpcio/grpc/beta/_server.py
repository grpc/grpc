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

"""Beta API server implementation."""

import threading

from grpc._links import service
from grpc.beta import interfaces
from grpc.framework.core import implementations as _core_implementations
from grpc.framework.crust import implementations as _crust_implementations
from grpc.framework.foundation import logging_pool
from grpc.framework.interfaces.base import base
from grpc.framework.interfaces.links import utilities

_DEFAULT_POOL_SIZE = 8
_DEFAULT_TIMEOUT = 300
_MAXIMUM_TIMEOUT = 24 * 60 * 60


class _GRPCServicer(base.Servicer):

  def __init__(self, delegate):
    self._delegate = delegate

  def service(self, group, method, context, output_operator):
    try:
      return self._delegate.service(group, method, context, output_operator)
    except base.NoSuchMethodError as e:
      if e.code is None and e.details is None:
        raise base.NoSuchMethodError(
            interfaces.StatusCode.UNIMPLEMENTED,
            b'Method "%s" of service "%s" not implemented!' % (method, group))
      else:
        raise


def _disassemble(grpc_link, end_link, pool, event, grace):
  grpc_link.begin_stop()
  end_link.stop(grace).wait()
  grpc_link.end_stop()
  grpc_link.join_link(utilities.NULL_LINK)
  end_link.join_link(utilities.NULL_LINK)
  if pool is not None:
    pool.shutdown(wait=True)
  event.set()


class Server(interfaces.Server):

  def __init__(self, grpc_link, end_link, pool):
    self._grpc_link = grpc_link
    self._end_link = end_link
    self._pool = pool

  def add_insecure_port(self, address):
    return self._grpc_link.add_port(address, None)

  def add_secure_port(self, address, server_credentials):
    return self._grpc_link.add_port(
        address, server_credentials._intermediary_low_credentials)  # pylint: disable=protected-access

  def _start(self):
    self._grpc_link.join_link(self._end_link)
    self._end_link.join_link(self._grpc_link)
    self._grpc_link.start()
    self._end_link.start()

  def _stop(self, grace):
    stop_event = threading.Event()
    if 0 < grace:
      disassembly_thread = threading.Thread(
          target=_disassemble,
          args=(
              self._grpc_link, self._end_link, self._pool, stop_event, grace,))
      disassembly_thread.start()
      return stop_event
    else:
      _disassemble(self._grpc_link, self._end_link, self._pool, stop_event, 0)
      return stop_event

  def start(self):
    self._start()

  def stop(self, grace):
    return self._stop(grace)

  def __enter__(self):
    self._start()
    return self

  def __exit__(self, exc_type, exc_val, exc_tb):
    self._stop(0).wait()
    return False


def server(
    implementations, multi_implementation, request_deserializers,
    response_serializers, thread_pool, thread_pool_size, default_timeout,
    maximum_timeout):
  if thread_pool is None:
    service_thread_pool = logging_pool.pool(
        _DEFAULT_POOL_SIZE if thread_pool_size is None else thread_pool_size)
    assembly_thread_pool = service_thread_pool
  else:
    service_thread_pool = thread_pool
    assembly_thread_pool = None

  servicer = _GRPCServicer(
      _crust_implementations.servicer(
          implementations, multi_implementation, service_thread_pool))

  grpc_link = service.service_link(request_deserializers, response_serializers)

  end_link = _core_implementations.service_end_link(
      servicer,
      _DEFAULT_TIMEOUT if default_timeout is None else default_timeout,
      _MAXIMUM_TIMEOUT if maximum_timeout is None else maximum_timeout)

  return Server(grpc_link, end_link, assembly_thread_pool)
