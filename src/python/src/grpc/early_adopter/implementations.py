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
from grpc.early_adopter import _assembly_utilities
from grpc.early_adopter import _reexport
from grpc.early_adopter import interfaces
from grpc.framework.assembly import implementations as _assembly_implementations


class _Server(interfaces.Server):

  def __init__(self, breakdown, port, private_key, certificate_chain):
    self._lock = threading.Lock()
    self._breakdown = breakdown
    self._port = port
    if private_key is None or certificate_chain is None:
      self._key_chain_pairs = ()
    else:
      self._key_chain_pairs = ((private_key, certificate_chain),)

    self._fore_link = None
    self._server = None

  def _start(self):
    with self._lock:
      if self._server is None:
        self._fore_link = _fore.activated_fore_link(
            self._port, self._breakdown.request_deserializers,
            self._breakdown.response_serializers, None, self._key_chain_pairs)

        self._server = _assembly_implementations.assemble_service(
            self._breakdown.implementations, self._fore_link)
        self._server.start()
      else:
        raise ValueError('Server currently running!')

  def _stop(self):
    with self._lock:
      if self._server is None:
        raise ValueError('Server not running!')
      else:
        self._server.stop()
        self._server = None
        self._fore_link = None

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

def _build_stub(breakdown, activated_rear_link):
  assembly_stub = _assembly_implementations.assemble_dynamic_inline_stub(
      breakdown.implementations, activated_rear_link)
  return _reexport.stub(assembly_stub, breakdown.cardinalities)


def _build_server(methods, port, private_key, certificate_chain):
  breakdown = _assembly_utilities.break_down_service(methods)
  return _Server(breakdown, port, private_key, certificate_chain)


def insecure_stub(methods, host, port):
  """Constructs an insecure interfaces.Stub.

  Args:
    methods: A dictionary from RPC method name to
      interfaces.RpcMethodInvocationDescription describing the RPCs to be
      supported by the created stub.
    host: The host to which to connect for RPC service.
    port: The port to which to connect for RPC service.

  Returns:
    An interfaces.Stub affording RPC invocation.
  """
  breakdown = _assembly_utilities.break_down_invocation(methods)
  activated_rear_link = _rear.activated_rear_link(
      host, port, breakdown.request_serializers,
      breakdown.response_deserializers)
  return _build_stub(breakdown, activated_rear_link)


def secure_stub(
    methods, host, port, root_certificates, private_key, certificate_chain):
  """Constructs an insecure interfaces.Stub.

  Args:
    methods: A dictionary from RPC method name to
      interfaces.RpcMethodInvocationDescription describing the RPCs to be
      supported by the created stub.
    host: The host to which to connect for RPC service.
    port: The port to which to connect for RPC service.
    root_certificates: The PEM-encoded root certificates or None to ask for
      them to be retrieved from a default location.
    private_key: The PEM-encoded private key to use or None if no private key
      should be used.
    certificate_chain: The PEM-encoded certificate chain to use or None if no
      certificate chain should be used.

  Returns:
    An interfaces.Stub affording RPC invocation.
  """
  breakdown = _assembly_utilities.break_down_invocation(methods)
  activated_rear_link = _rear.secure_activated_rear_link(
      host, port, breakdown.request_serializers,
      breakdown.response_deserializers, root_certificates, private_key,
      certificate_chain)
  return _build_stub(breakdown, activated_rear_link)


def insecure_server(methods, port):
  """Constructs an insecure interfaces.Server.

  Args:
    methods: A dictionary from RPC method name to
      interfaces.RpcMethodServiceDescription describing the RPCs to
      be serviced by the created server.
    port: The desired port on which to serve or zero to ask for a port to
      be automatically selected.

  Returns:
    An interfaces.Server that will run with no security and
      service unsecured raw requests.
  """
  return _build_server(methods, port, None, None)


def secure_server(methods, port, private_key, certificate_chain):
  """Constructs a secure interfaces.Server.

  Args:
    methods: A dictionary from RPC method name to
      interfaces.RpcMethodServiceDescription describing the RPCs to
      be serviced by the created server.
    port: The port on which to serve or zero to ask for a port to be
      automatically selected.
    private_key: A pem-encoded private key.
    certificate_chain: A pem-encoded certificate chain.

  Returns:
    An interfaces.Server that will serve secure traffic.
  """
  return _build_server(methods, port, private_key, certificate_chain)
