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

"""Reference implementation for health checking in gRPC Python."""

import abc
import enum
import threading

from grpc.health.v1 import health_pb2


@enum.unique
class HealthStatus(enum.Enum):
  """Statuses for a service mirroring the reference health.proto's values."""
  UNKNOWN = health_pb2.HealthCheckResponse.UNKNOWN
  SERVING = health_pb2.HealthCheckResponse.SERVING
  NOT_SERVING = health_pb2.HealthCheckResponse.NOT_SERVING


class _HealthServicer(health_pb2.EarlyAdopterHealthServicer):
  """Servicer handling RPCs for service statuses."""

  def __init__(self):
    self._server_status_lock = threading.Lock()
    self._server_status = {}

  def Check(self, request, context):
    with self._server_status_lock:
      if request.service not in self._server_status:
        # TODO(atash): once the Python API has a way of setting the server
        # status, bring us into conformance with the health check spec by
        # returning the NOT_FOUND status here.
        raise NotImplementedError()
      else:
        return health_pb2.HealthCheckResponse(
            status=self._server_status[request.service].value)

  def set(service, status):
    if not isinstance(status, HealthStatus):
      raise TypeError('expected grpc.health.v1.health.HealthStatus '
                      'for argument `status` but got {}'.format(status))
    with self._server_status_lock:
      self._server_status[service] = status


class HealthServer(health_pb2.EarlyAdopterHealthServer):
  """Interface for the reference gRPC Python health server."""
  __metaclass__ = abc.ABCMeta

  @abc.abstractmethod
  def start(self):
    raise NotImplementedError()

  @abc.abstractmethod
  def stop(self):
    raise NotImplementedError()

  @abc.abstractmethod
  def set(self, service, status):
    """Set the status of the given service.

    Args:
      service (str): service name of the service to set the reported status of
      status (HealthStatus): status to set for the specified service
    """
    raise NotImplementedError()


class _HealthServerImplementation(HealthServer):
  """Implementation for the reference gRPC Python health server."""

  def __init__(self, server, servicer):
    self._server = server
    self._servicer = servicer

  def start(self):
    self._server.start()

  def stop(self):
    self._server.stop()

  def set(self, service, status):
    self._servicer.set(service, status)


def create_Health_server(port, private_key=None, certificate_chain=None):
  """Get a HealthServer instance.

  Args:
    port (int): port number passed through to health_pb2 server creation
      routine.
    private_key (str): to-be-created server's desired private key
    certificate_chain (str): to-be-created server's desired certificate chain

  Returns:
    An instance of HealthServer (conforming thus to
    EarlyAdopterHealthServer and providing a method to set server status)."""
  servicer = _HealthServicer()
  server = health_pb2.early_adopter_create_Health_server(
      servicer, port=port, private_key=private_key,
      certificate_chain=certificate_chain)
  return _HealthServerImplementation(server, servicer)
