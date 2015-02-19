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

from grpc._adapter import fore
from grpc.framework.base.packets import implementations as _tickets_implementations
from grpc.framework.face import implementations as _face_implementations
from grpc.framework.foundation import logging_pool
from grpc.early_adopter import _face_utilities
from grpc.early_adopter import interfaces

_MEGA_TIMEOUT = 60 * 60 * 24
_THREAD_POOL_SIZE = 80


class _Server(interfaces.Server):

  def __init__(self, breakdown, port, private_key, certificate_chain):
    self._lock = threading.Lock()
    self._breakdown = breakdown
    self._port = port
    self._private_key = private_key
    self._certificate_chain = certificate_chain

    self._pool = None
    self._fore_link = None
    self._back = None

  def start(self):
    """See interfaces.Server.start for specification."""
    with self._lock:
      if self._pool is None:
        self._pool = logging_pool.pool(_THREAD_POOL_SIZE)
        servicer = _face_implementations.servicer(
            self._pool,
            inline_value_in_value_out_methods=self._breakdown.unary_unary_methods,
            inline_value_in_stream_out_methods=self._breakdown.unary_stream_methods,
            inline_stream_in_value_out_methods=self._breakdown.stream_unary_methods,
            inline_stream_in_stream_out_methods=self._breakdown.stream_stream_methods)
        self._fore_link = fore.ForeLink(
            self._pool, self._breakdown.request_deserializers,
            self._breakdown.response_serializers, None,
            ((self._private_key, self._certificate_chain),), port=self._port)
        port = self._fore_link.start()
        self._back = _tickets_implementations.back(
            servicer, self._pool, self._pool, self._pool, _MEGA_TIMEOUT,
            _MEGA_TIMEOUT)
        self._fore_link.join_rear_link(self._back)
        self._back.join_fore_link(self._fore_link)
        return port
      else:
        raise ValueError('Server currently running!')

  def stop(self):
    """See interfaces.Server.stop for specification."""
    with self._lock:
      if self._pool is None:
        raise ValueError('Server not running!')
      else:
        self._fore_link.stop()
        self._pool.shutdown(wait=True)
        self._pool = None


def _build_server(methods, port, private_key, certificate_chain):
  breakdown = _face_utilities.server_break_down(methods)
  return _Server(breakdown, port, private_key, certificate_chain)


def insecure_server(methods, port):
  """Constructs an insecure interfaces.Server.

  Args:
    methods: A dictionary from RPC method name to
      interfaces.ServerRpcMethod object describing the RPCs to
      be serviced by the created server.
    port: The port on which to serve.

  Returns:
    An interfaces.Server that will run with no security and
      service unsecured raw requests.
  """
  return _build_server(methods, port, None, None)


def secure_server(methods, port, private_key, certificate_chain):
  """Constructs a secure interfaces.Server.

  Args:
    methods: A dictionary from RPC method name to
      interfaces.ServerRpcMethod object describing the RPCs to
      be serviced by the created server.
    port: The port on which to serve.
    private_key: A pem-encoded private key.
    certificate_chain: A pem-encoded certificate chain.

  Returns:
    An interfaces.Server that will serve secure traffic.
  """
  return _build_server(methods, port, private_key, certificate_chain)
