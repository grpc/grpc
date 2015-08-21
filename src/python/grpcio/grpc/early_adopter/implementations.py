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

"""Entry points into GRPC."""

import threading

from grpc._adapter import fore as _fore
from grpc._adapter import rear as _rear
from grpc.framework.alpha import _face_utilities
from grpc.framework.alpha import _reexport
from grpc.framework.alpha import interfaces
from grpc.framework.base import implementations as _base_implementations
from grpc.framework.base import util as _base_utilities
from grpc.framework.face import implementations as _face_implementations
from grpc.framework.foundation import logging_pool

_DEFAULT_THREAD_POOL_SIZE = 8
_ONE_DAY_IN_SECONDS = 24 * 60 * 60


class _Server(interfaces.Server):

  def __init__(
        self, breakdown, port, private_key, certificate_chain,
        thread_pool_size=_DEFAULT_THREAD_POOL_SIZE):
    self._lock = threading.Lock()
    self._breakdown = breakdown
    self._port = port
    if private_key is None or certificate_chain is None:
      self._key_chain_pairs = ()
    else:
      self._key_chain_pairs = ((private_key, certificate_chain),)

    self._pool_size = thread_pool_size
    self._pool = None
    self._back = None
    self._fore_link = None

  def _start(self):
    with self._lock:
      if self._pool is None:
        self._pool = logging_pool.pool(self._pool_size)
        servicer = _face_implementations.servicer(
            self._pool, self._breakdown.implementations, None)
        self._back = _base_implementations.back_link(
            servicer, self._pool, self._pool, self._pool, _ONE_DAY_IN_SECONDS,
            _ONE_DAY_IN_SECONDS)
        self._fore_link = _fore.ForeLink(
            self._pool, self._breakdown.request_deserializers,
            self._breakdown.response_serializers, None, self._key_chain_pairs,
            port=self._port)
        self._back.join_fore_link(self._fore_link)
        self._fore_link.join_rear_link(self._back)
        self._fore_link.start()
      else:
        raise ValueError('Server currently running!')

  def _stop(self):
    with self._lock:
      if self._pool is None:
        raise ValueError('Server not running!')
      else:
        self._fore_link.stop()
        _base_utilities.wait_for_idle(self._back)
        self._pool.shutdown(wait=True)
        self._fore_link = None
        self._back = None
        self._pool = None

  def __enter__(self):
    self._start()
    return self

  def __exit__(self, exc_type, exc_val, exc_tb):
    self._stop()
    return False

  def start(self):
    self._start()

  def stop(self):
    self._stop()

  def port(self):
    with self._lock:
      return self._fore_link.port()


class _Stub(interfaces.Stub):

  def __init__(
      self, breakdown, host, port, secure, root_certificates, private_key,
      certificate_chain, metadata_transformer=None, server_host_override=None,
      thread_pool_size=_DEFAULT_THREAD_POOL_SIZE):
    self._lock = threading.Lock()
    self._breakdown = breakdown
    self._host = host
    self._port = port
    self._secure = secure
    self._root_certificates = root_certificates
    self._private_key = private_key
    self._certificate_chain = certificate_chain
    self._metadata_transformer = metadata_transformer
    self._server_host_override = server_host_override

    self._pool_size = thread_pool_size
    self._pool = None
    self._front = None
    self._rear_link = None
    self._understub = None

  def __enter__(self):
    with self._lock:
      if self._pool is None:
        self._pool = logging_pool.pool(self._pool_size)
        self._front = _base_implementations.front_link(
            self._pool, self._pool, self._pool)
        self._rear_link = _rear.RearLink(
            self._host, self._port, self._pool,
            self._breakdown.request_serializers,
            self._breakdown.response_deserializers, self._secure,
            self._root_certificates, self._private_key, self._certificate_chain,
            metadata_transformer=self._metadata_transformer,
            server_host_override=self._server_host_override)
        self._front.join_rear_link(self._rear_link)
        self._rear_link.join_fore_link(self._front)
        self._rear_link.start()
        self._understub = _face_implementations.dynamic_stub(
            self._breakdown.face_cardinalities, self._front, self._pool, '')
      else:
        raise ValueError('Tried to __enter__ already-__enter__ed Stub!')
    return self

  def __exit__(self, exc_type, exc_val, exc_tb):
    with self._lock:
      if self._pool is None:
        raise ValueError('Tried to __exit__ non-__enter__ed Stub!')
      else:
        self._rear_link.stop()
        _base_utilities.wait_for_idle(self._front)
        self._pool.shutdown(wait=True)
        self._rear_link = None
        self._front = None
        self._pool = None
        self._understub = None
    return False

  def __getattr__(self, attr):
    with self._lock:
      if self._pool is None:
        raise ValueError('Tried to __getattr__ non-__enter__ed Stub!')
      else:
        method_cardinality = self._breakdown.cardinalities.get(attr)
        underlying_attr = getattr(
            self._understub, self._breakdown.qualified_names.get(attr), None)
        if method_cardinality is interfaces.Cardinality.UNARY_UNARY:
          return _reexport.unary_unary_sync_async(underlying_attr)
        elif method_cardinality is interfaces.Cardinality.UNARY_STREAM:
          return lambda request, timeout: _reexport.cancellable_iterator(
              underlying_attr(request, timeout))
        elif method_cardinality is interfaces.Cardinality.STREAM_UNARY:
          return _reexport.stream_unary_sync_async(underlying_attr)
        elif method_cardinality is interfaces.Cardinality.STREAM_STREAM:
          return lambda request_iterator, timeout: (
              _reexport.cancellable_iterator(underlying_attr(
                  request_iterator, timeout)))
        else:
          raise AttributeError(attr)


def stub(
    service_name, methods, host, port, metadata_transformer=None, secure=False,
    root_certificates=None, private_key=None, certificate_chain=None,
    server_host_override=None, thread_pool_size=_DEFAULT_THREAD_POOL_SIZE):
  """Constructs an interfaces.Stub.

  Args:
    service_name: The package-qualified full name of the service.
    methods: A dictionary from RPC method name to
      interfaces.RpcMethodInvocationDescription describing the RPCs to be
      supported by the created stub. The RPC method names in the dictionary are
      not qualified by the service name or decorated in any other way.
    host: The host to which to connect for RPC service.
    port: The port to which to connect for RPC service.
    metadata_transformer: A callable that given a metadata object produces
      another metadata object to be used in the underlying communication on the
      wire.
    secure: Whether or not to construct the stub with a secure connection.
    root_certificates: The PEM-encoded root certificates or None to ask for
      them to be retrieved from a default location.
    private_key: The PEM-encoded private key to use or None if no private key
      should be used.
    certificate_chain: The PEM-encoded certificate chain to use or None if no
      certificate chain should be used.
    server_host_override: (For testing only) the target name used for SSL
      host name checking.
    thread_pool_size: The maximum number of threads to allow in the backing
      thread pool.

  Returns:
    An interfaces.Stub affording RPC invocation.
  """
  breakdown = _face_utilities.break_down_invocation(service_name, methods)
  return _Stub(
      breakdown, host, port, secure, root_certificates, private_key,
      certificate_chain, server_host_override=server_host_override,
      metadata_transformer=metadata_transformer,
      thread_pool_size=thread_pool_size)


def server(
    service_name, methods, port, private_key=None, certificate_chain=None,
    thread_pool_size=_DEFAULT_THREAD_POOL_SIZE):
  """Constructs an interfaces.Server.

  Args:
    service_name: The package-qualified full name of the service.
    methods: A dictionary from RPC method name to
      interfaces.RpcMethodServiceDescription describing the RPCs to
      be serviced by the created server. The RPC method names in the dictionary
      are not qualified by the service name or decorated in any other way.
    port: The port on which to serve or zero to ask for a port to be
      automatically selected.
    private_key: A pem-encoded private key, or None for an insecure server.
    certificate_chain: A pem-encoded certificate chain, or None for an insecure
      server.
    thread_pool_size: The maximum number of threads to allow in the backing
      thread pool.

  Returns:
    An interfaces.Server that will serve secure traffic.
  """
  breakdown = _face_utilities.break_down_service(service_name, methods)
  return _Server(breakdown, port, private_key, certificate_chain,
      thread_pool_size=thread_pool_size)
