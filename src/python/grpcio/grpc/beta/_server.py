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


def _set_event():
  event = threading.Event()
  event.set()
  return event


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


class _Server(interfaces.Server):

  def __init__(
      self, implementations, multi_implementation, pool, pool_size,
      default_timeout, maximum_timeout, grpc_link):
    self._lock = threading.Lock()
    self._implementations = implementations
    self._multi_implementation = multi_implementation
    self._customer_pool = pool
    self._pool_size = pool_size
    self._default_timeout = default_timeout
    self._maximum_timeout = maximum_timeout
    self._grpc_link = grpc_link

    self._end_link = None
    self._stop_events = None
    self._pool = None

  def _start(self):
    with self._lock:
      if self._end_link is not None:
        raise ValueError('Cannot start already-started server!')

      if self._customer_pool is None:
        self._pool = logging_pool.pool(self._pool_size)
        assembly_pool = self._pool
      else:
        assembly_pool = self._customer_pool

      servicer = _GRPCServicer(
          _crust_implementations.servicer(
              self._implementations, self._multi_implementation, assembly_pool))

      self._end_link = _core_implementations.service_end_link(
          servicer, self._default_timeout, self._maximum_timeout)

      self._grpc_link.join_link(self._end_link)
      self._end_link.join_link(self._grpc_link)
      self._grpc_link.start()
      self._end_link.start()

  def _dissociate_links_and_shut_down_pool(self):
    self._grpc_link.end_stop()
    self._grpc_link.join_link(utilities.NULL_LINK)
    self._end_link.join_link(utilities.NULL_LINK)
    self._end_link = None
    if self._pool is not None:
      self._pool.shutdown(wait=True)
    self._pool = None

  def _stop_stopping(self):
    self._dissociate_links_and_shut_down_pool()
    for stop_event in self._stop_events:
      stop_event.set()
    self._stop_events = None

  def _stop_started(self):
    self._grpc_link.begin_stop()
    self._end_link.stop(0).wait()
    self._dissociate_links_and_shut_down_pool()

  def _foreign_thread_stop(self, end_stop_event, stop_events):
    end_stop_event.wait()
    with self._lock:
      if self._stop_events is stop_events:
        self._stop_stopping()

  def _schedule_stop(self, grace):
    with self._lock:
      if self._end_link is None:
        return _set_event()
      server_stop_event = threading.Event()
      if self._stop_events is None:
        self._stop_events = [server_stop_event]
        self._grpc_link.begin_stop()
      else:
        self._stop_events.append(server_stop_event)
      end_stop_event = self._end_link.stop(grace)
      end_stop_thread = threading.Thread(
          target=self._foreign_thread_stop,
          args=(end_stop_event, self._stop_events))
      end_stop_thread.start()
      return server_stop_event

  def _stop_now(self):
    with self._lock:
      if self._end_link is not None:
        if self._stop_events is None:
          self._stop_started()
        else:
          self._stop_stopping()

  def add_insecure_port(self, address):
    with self._lock:
      if self._end_link is None:
        return self._grpc_link.add_port(address, None)
      else:
        raise ValueError('Can\'t add port to serving server!')

  def add_secure_port(self, address, server_credentials):
    with self._lock:
      if self._end_link is None:
        return self._grpc_link.add_port(
            address, server_credentials._low_credentials)  # pylint: disable=protected-access
      else:
        raise ValueError('Can\'t add port to serving server!')

  def start(self):
    self._start()

  def stop(self, grace):
    if 0 < grace:
      return self._schedule_stop(grace)
    else:
      self._stop_now()
      return _set_event()

  def __enter__(self):
    self._start()
    return self

  def __exit__(self, exc_type, exc_val, exc_tb):
    self._stop_now()
    return False

  def __del__(self):
    self._stop_now()


def server(
    implementations, multi_implementation, request_deserializers,
    response_serializers, thread_pool, thread_pool_size, default_timeout,
    maximum_timeout):
  grpc_link = service.service_link(request_deserializers, response_serializers)
  return _Server(
      implementations, multi_implementation, thread_pool,
      _DEFAULT_POOL_SIZE if thread_pool_size is None else thread_pool_size,
      _DEFAULT_TIMEOUT if default_timeout is None else default_timeout,
      _MAXIMUM_TIMEOUT if maximum_timeout is None else maximum_timeout,
      grpc_link)
