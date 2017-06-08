# Copyright 2015 gRPC authors.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
"""Reference implementation for health checking in gRPC Python."""

import threading

import grpc

from grpc_health.v1 import health_pb2
from grpc_health.v1 import health_pb2_grpc


class HealthServicer(health_pb2_grpc.HealthServicer):
    """Servicer handling RPCs for service statuses."""

    def __init__(self):
        self._server_status_lock = threading.Lock()
        self._server_status = {}

    def Check(self, request, context):
        with self._server_status_lock:
            status = self._server_status.get(request.service)
            if status is None:
                context.set_code(grpc.StatusCode.NOT_FOUND)
                return health_pb2.HealthCheckResponse()
            else:
                return health_pb2.HealthCheckResponse(status=status)

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
