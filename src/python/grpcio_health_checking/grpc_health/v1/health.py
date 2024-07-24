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
from concurrent import futures
import sys
import threading
from typing import Any, Callable, Dict, Optional, Set

import grpc
from grpc_health.v1 import health_pb2 as _health_pb2
from grpc_health.v1 import health_pb2_grpc as _health_pb2_grpc

if sys.version_info[0] >= 3 and sys.version_info[1] >= 6:
    # Exposes AsyncHealthServicer as public API.
    from . import _async as aio  # pylint: disable=unused-import

# The service name of the health checking servicer.
SERVICE_NAME = _health_pb2.DESCRIPTOR.services_by_name["Health"].full_name
# The entry of overall health for the entire server.
OVERALL_HEALTH = ""


class _Watcher():
    _condition: threading.Condition
    _responses: collections.deque
    _open: bool

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

    def add(self, response: Any) -> None:
        with self._condition:
            self._responses.append(response)
            self._condition.notify()

    def close(self) -> None:
        with self._condition:
            self._open = False
            self._condition.notify()


def _watcher_to_send_response_callback_adapter(
    watcher: _Watcher
) -> Callable[[Optional[_health_pb2.HealthCheckResponse]], None]:

    def send_response_callback(response):
        if response is None:
            watcher.close()
        else:
            watcher.add(response)

    return send_response_callback


class HealthServicer(_health_pb2_grpc.HealthServicer):
    """Servicer handling RPCs for service statuses."""
    _lock: threading.RLock
    _server_status: Dict[str, int]
    _send_response_callbacks: Dict[str, Set[Callable[
        [_health_pb2.HealthCheckResponse], None]]]
    _gracefully_shutting_down: bool

    def __init__(self,
                 experimental_non_blocking: bool = True,
                 experimental_thread_pool: Optional[
                     futures.ThreadPoolExecutor] = None):
        self._lock = threading.RLock()
        self._server_status = {"": _health_pb2.HealthCheckResponse.SERVING}
        self._send_response_callbacks = {}
        self.Watch.__func__.experimental_non_blocking = experimental_non_blocking  # type: ignore[attr-defined]
        self.Watch.__func__.experimental_thread_pool = experimental_thread_pool  # type: ignore[attr-defined]
        self._gracefully_shutting_down = False

    def _on_close_callback(self, send_response_callback: Callable[
        [_health_pb2.HealthCheckResponse], None],
                           service: str) -> Callable[[], None]:

        def callback():
            with self._lock:
                self._send_response_callbacks[service].remove(
                    send_response_callback
                )
            send_response_callback(None)

        return callback

    def Check(self, request: _health_pb2.HealthCheckRequest,
              context: grpc.ServicerContext) -> _health_pb2.HealthCheckResponse:
        with self._lock:
            status = self._server_status.get(request.service)
            if status is None:
                context.set_code(grpc.StatusCode.NOT_FOUND)
                return _health_pb2.HealthCheckResponse()
            else:
                return _health_pb2.HealthCheckResponse(status=status)

    # pylint: disable=arguments-differ
    def Watch(
        self,
        request: _health_pb2.HealthCheckRequest,
        context: grpc.ServicerContext,
        send_response_callback: Optional[Callable[
            [_health_pb2.HealthCheckResponse], None]] = None
    ) -> Optional[_Watcher]:
        blocking_watcher = None
        if send_response_callback is None:
            # The server does not support the experimental_non_blocking
            # parameter. For backwards compatibility, return a blocking response
            # generator.
            blocking_watcher = _Watcher()
            send_response_callback = _watcher_to_send_response_callback_adapter(
                blocking_watcher
            )
        service = request.service
        with self._lock:
            status = self._server_status.get(service)
            if status is None:
                status = (
                    _health_pb2.HealthCheckResponse.SERVICE_UNKNOWN
                )  # pylint: disable=no-member
            send_response_callback(
                _health_pb2.HealthCheckResponse(status=status)
            )
            if service not in self._send_response_callbacks:
                self._send_response_callbacks[service] = set()
            self._send_response_callbacks[service].add(send_response_callback)
            context.add_callback(
                self._on_close_callback(send_response_callback, service)
            )
        return blocking_watcher

    def set(self, service: str, status: int) -> None:
        """Sets the status of a service.

        Args:
          service: string, the name of the service.
          status: HealthCheckResponse.status enum value indicating the status of
            the service
        """
        with self._lock:
            if self._gracefully_shutting_down:
                return
            else:
                self._server_status[service] = status
                if service in self._send_response_callbacks:
                    for send_response_callback in self._send_response_callbacks[
                        service
                    ]:
                        send_response_callback(
                            _health_pb2.HealthCheckResponse(status=status)
                        )

    def enter_graceful_shutdown(self) -> None:
        """Permanently sets the status of all services to NOT_SERVING.

        This should be invoked when the server is entering a graceful shutdown
        period. After this method is invoked, future attempts to set the status
        of a service will be ignored.

        This is an EXPERIMENTAL API.
        """
        with self._lock:
            if self._gracefully_shutting_down:
                return
            else:
                for service in self._server_status:
                    self.set(
                        service, _health_pb2.HealthCheckResponse.NOT_SERVING
                    )  # pylint: disable=no-member
                self._gracefully_shutting_down = True
