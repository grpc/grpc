# Copyright 2026 gRPC authors.
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
"""Concurrency tests of grpc_health.v1.health."""

import logging
import unittest

from grpc_health.v1 import health
from grpc_health.v1 import health_pb2
from grpc_health.v1 import health_pb2_grpc

from concurrency_tests._async_concurrency_base import ITERATIONS_PER_TASK
from concurrency_tests._async_concurrency_base import AsyncConcurrencyTestCase


_RPC_TIMEOUT = 30

_SERVICES = tuple(f"service_{i}" for i in range(8))
_TOGGLED_STATUSES = (
    health_pb2.HealthCheckResponse.SERVING,
    health_pb2.HealthCheckResponse.NOT_SERVING,
)


class AsyncHealthServicerConcurrencyTest(AsyncConcurrencyTestCase):
    async def setUp(self) -> None:
        await super().setUp()
        self._servicer = health.aio.HealthServicer()
        for service in _SERVICES:
            await self._servicer.set(
                service, health_pb2.HealthCheckResponse.SERVING
            )

        _, self._stub = await self.start_server(
            self._servicer,
            health_pb2_grpc.add_HealthServicer_to_server,
            health_pb2_grpc.HealthStub,
        )

    async def _check(self, index):
        for i in range(ITERATIONS_PER_TASK):
            service = _SERVICES[(index + i) % len(_SERVICES)]
            response = await self._stub.Check(
                health_pb2.HealthCheckRequest(service=service),
                timeout=_RPC_TIMEOUT,
            )
            self.assertIn(response.status, _TOGGLED_STATUSES)

    async def _set(self, index):
        for i in range(ITERATIONS_PER_TASK):
            await self._servicer.set(
                _SERVICES[(index + i) % len(_SERVICES)],
                _TOGGLED_STATUSES[i % len(_TOGGLED_STATUSES)],
            )

    async def _watch_first_response_then_cancel(self, index):
        service = _SERVICES[(index) % len(_SERVICES)]
        call = self._stub.Watch(
            health_pb2.HealthCheckRequest(service=service),
            timeout=_RPC_TIMEOUT,
        )
        try:
            response = await call.read()
            self.assertIn(response.status, _TOGGLED_STATUSES)
        finally:
            call.cancel()

    async def _watch_shared_service(self, unused_index):
        service = _SERVICES[0]
        call = self._stub.Watch(
            health_pb2.HealthCheckRequest(service=service),
            timeout=_RPC_TIMEOUT,
        )
        try:
            response = await call.read()
            self.assertIn(response.status, _TOGGLED_STATUSES)
        finally:
            call.cancel()

    async def _graceful_shutdown(self, unused_index):
        await self._servicer.enter_graceful_shutdown()

    async def test_concurrent_check(self):
        await self.run_workers(self._check)

    async def test_concurrent_check_and_set(self):
        await self.run_workers(self._set, self._check)

    async def test_concurrent_watch_and_set(self):
        await self.run_workers(self._watch_first_response_then_cancel, self._set)

    async def test_concurrent_watchers_same_service(self):
        await self.run_workers(self._watch_shared_service, self._set)

    async def test_concurrent_graceful_shutdown_and_set_and_watch(self):
        await self.run_workers(
            self._graceful_shutdown,
            self._watch_first_response_then_cancel,
            self._set
        )


if __name__ == "__main__":
    logging.basicConfig()
    unittest.main(verbosity=2)
