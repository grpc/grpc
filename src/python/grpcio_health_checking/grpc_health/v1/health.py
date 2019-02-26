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

from grpc_health.v1 import health_pb2 as _health_pb2
from grpc_health.v1 import health_pb2_grpc as _health_pb2_grpc

SERVICE_NAME = _health_pb2.DESCRIPTOR.services_by_name['Health'].full_name


class _Watcher():

    def __init__(self):
        self._condition = threading.Condition()
        self._responses = list()
        self._open = True

    def __iter__(self):
        return self

    def _next(self):
        with self._condition:
            while not self._responses and self._open:
                self._condition.wait()
            if self._responses:
                return self._responses.pop(0)
            else:
                raise StopIteration()

    def next(self):
        return self._next()

    def __next__(self):
        return self._next()

    def add(self, response):
        with self._condition:
            self._responses.append(response)
            self._condition.notify()

    def close(self):
        with self._condition:
            self._open = False
            self._condition.notify()


class HealthServicer(_health_pb2_grpc.HealthServicer):
    """Servicer handling RPCs for service statuses."""

    def __init__(self):
        self._lock = threading.RLock()
        self._server_status = {}
        self._watchers = {}

    def _on_close_callback(self, watcher, service):

        def callback():
            with self._lock:
                self._watchers[service].remove(watcher)
            watcher.close()

        return callback

    def Check(self, request, context):
        with self._lock:
            status = self._server_status.get(request.service)
            if status is None:
                context.set_code(grpc.StatusCode.NOT_FOUND)
                return _health_pb2.HealthCheckResponse()
            else:
                return _health_pb2.HealthCheckResponse(status=status)

    def Watch(self, request, context):
        service = request.service
        with self._lock:
            status = self._server_status.get(service)
            if status is None:
                status = _health_pb2.HealthCheckResponse.SERVICE_UNKNOWN  # pylint: disable=no-member
            watcher = _Watcher()
            watcher.add(_health_pb2.HealthCheckResponse(status=status))
            if service not in self._watchers:
                self._watchers[service] = set()
            self._watchers[service].add(watcher)
            context.add_callback(self._on_close_callback(watcher, service))
        return watcher

    def set(self, service, status):
        """Sets the status of a service.

        Args:
          service: string, the name of the service. NOTE, '' must be set.
          status: HealthCheckResponse.status enum value indicating the status of
            the service
        """
        with self._lock:
            self._server_status[service] = status
            if service in self._watchers:
                for watcher in self._watchers[service]:
                    watcher.add(_health_pb2.HealthCheckResponse(status=status))
