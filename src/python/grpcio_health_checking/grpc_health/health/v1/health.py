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

import threading

from grpc_health.health.v1 import health_pb2


class HealthServicer(health_pb2.BetaHealthServicer):
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
            status=self._server_status[request.service])

  def set(self, service, status):
    """Sets the status of a service.

    Args:
        service: string, the name of the service.
            NOTE, '' must be set.
        status: HealthCheckResponse.status enum value indicating
            the status of the service
    """
    with self._server_status_lock:
      self._server_status[service] = status

