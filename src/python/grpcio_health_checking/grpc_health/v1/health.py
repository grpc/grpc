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

import collections
import threading

import grpc

from grpc_health.v1 import health_pb2 as _health_pb2
from grpc_health.v1 import health_pb2_grpc as _health_pb2_grpc

SERVICE_NAME = _health_pb2.DESCRIPTOR.services_by_name['Health'].full_name


class _Watcher():

    def __init__(self):
        self._condition = threading.Condition()
        self._responses = collections.deque()
        self._open = True

    def __iter__(self):
        return self

    def _next(self):
        with self._condition:
            while not self._responses and self._open:
                self._condition.wait()
            if self._responses:
                return self._responses.popleft()
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


def _watcher_to_on_next_callback_adapter(watcher):

    def on_next_callback(response):
        if response is None:
            watcher.close()
        else:
            watcher.add(response)

    return on_next_callback


class HealthServicer(_health_pb2_grpc.HealthServicer):
    """Servicer handling RPCs for service statuses."""

    def __init__(self,
                 experimental_non_blocking=True,
                 experimental_thread_pool=None):
        self._lock = threading.RLock()
        self._server_status = {}
        self._on_next_callbacks = {}
        self.Watch.__func__.experimental_non_blocking = experimental_non_blocking
        self.Watch.__func__.experimental_thread_pool = experimental_thread_pool

    def _on_close_callback(self, on_next_callback, service):

        def callback():
            with self._lock:
                self._on_next_callbacks[service].remove(on_next_callback)
            on_next_callback(None)

        return callback

    def Check(self, request, context):
        with self._lock:
            status = self._server_status.get(request.service)
            if status is None:
                context.set_code(grpc.StatusCode.NOT_FOUND)
                return _health_pb2.HealthCheckResponse()
            else:
                return _health_pb2.HealthCheckResponse(status=status)

    # pylint: disable=arguments-differ
    def Watch(self, request, context, on_next_callback=None):
        blocking_watcher = None
        if on_next_callback is None:
            # The server does not support the experimental_non_blocking
            # parameter. For backwards compatibility, return a blocking response
            # generator.
            blocking_watcher = _Watcher()
            on_next_callback = _watcher_to_on_next_callback_adapter(
                blocking_watcher)
        service = request.service
        with self._lock:
            status = self._server_status.get(service)
            if status is None:
                status = _health_pb2.HealthCheckResponse.SERVICE_UNKNOWN  # pylint: disable=no-member
            on_next_callback(_health_pb2.HealthCheckResponse(status=status))
            if service not in self._on_next_callbacks:
                self._on_next_callbacks[service] = set()
            self._on_next_callbacks[service].add(on_next_callback)
            context.add_callback(
                self._on_close_callback(on_next_callback, service))
        return blocking_watcher

    def set(self, service, status):
        """Sets the status of a service.

        Args:
          service: string, the name of the service. NOTE, '' must be set.
          status: HealthCheckResponse.status enum value indicating the status of
            the service
        """
        with self._lock:
            self._server_status[service] = status
            if service in self._on_next_callbacks:
                for on_next_callback in self._on_next_callbacks[service]:
                    on_next_callback(
                        _health_pb2.HealthCheckResponse(status=status))
