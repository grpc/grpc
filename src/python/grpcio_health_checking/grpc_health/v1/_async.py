# Copyright 2020 The gRPC Authors
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

import asyncio
import collections
from typing import Mapping, AbstractSet

import grpc

from grpc_health.v1 import health_pb2 as _health_pb2
from grpc_health.v1 import health_pb2_grpc as _health_pb2_grpc


class AsyncHealthServicer(_health_pb2_grpc.HealthServicer):
    """An AsyncIO implementation of health checking servicer."""
    _server_status: Mapping[str,
                            '_health_pb2.HealthCheckResponse.ServingStatus']
    _server_watchers: Mapping[str, AbstractSet[asyncio.Queue]]
    _gracefully_shutting_down: bool

    def __init__(self):
        self._server_status = dict()
        self._server_watchers = collections.defaultdict(set)
        self._gracefully_shutting_down = False

    async def Check(self, request: _health_pb2.HealthCheckRequest, context):
        status = self._server_status.get(request.service)

        if status is None:
            await context.abort(grpc.StatusCode.NOT_FOUND)
        else:
            return _health_pb2.HealthCheckResponse(status=status)

    async def Watch(self, request: _health_pb2.HealthCheckRequest, context):
        queue = asyncio.Queue()
        self._server_watchers[request.service].add(queue)

        try:
            status = self._server_status.get(
                request.service,
                _health_pb2.HealthCheckResponse.SERVICE_UNKNOWN)
            while True:
                # Responds with current health state
                await context.write(
                    _health_pb2.HealthCheckResponse(status=status))

                # Polling on health state changes
                status = await queue.get()
        finally:
            self._server_watchers[request.service].remove(queue)
            if not self._server_watchers[request.service]:
                del self._server_watchers[request.service]

    def _set(self, service: str,
             status: _health_pb2.HealthCheckResponse.ServingStatus):
        self._server_status[service] = status

        if service in self._server_watchers:
            # Only iterate through the watchers if there is at least one.
            # Otherwise, it creates empty sets.
            for watcher in self._server_watchers[service]:
                watcher.put_nowait(status)

    def set(self, service: str,
            status: _health_pb2.HealthCheckResponse.ServingStatus):
        """Sets the status of a service.

        Args:
          service: string, the name of the service. NOTE, '' must be set.
          status: HealthCheckResponse.status enum value indicating the status of
            the service
        """
        if self._gracefully_shutting_down:
            return
        else:
            self._set(service, status)

    async def enter_graceful_shutdown(self):
        """Permanently sets the status of all services to NOT_SERVING.

        This should be invoked when the server is entering a graceful shutdown
        period. After this method is invoked, future attempts to set the status
        of a service will be ignored.

        This is an EXPERIMENTAL API.
        """
        if self._gracefully_shutting_down:
            return
        else:
            self._gracefully_shutting_down = True
            for service in self._server_status:
                self._set(service, _health_pb2.HealthCheckResponse.NOT_SERVING)
