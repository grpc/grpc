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
"""Regression test for the AsyncIO client observability tracer-setup path.

Setting up the client call tracer must not block the event loop on the
process-global observability _plugin_lock. Before the fix, every grpc.aio
client call acquired that lock on the event loop thread, so any other thread
holding it (e.g. a metrics exporter) froze the entire loop off-CPU.
"""

import asyncio
import logging
import threading
import time
import unittest

from grpc import _observability
from grpc.experimental import aio

from tests_aio.unit._test_base import AioTestBase

# While another thread holds _plugin_lock, a pre-fix build blocks call
# construction for ~_LOCK_HOLD_S; the fixed build returns near-instantly.
_LOCK_HOLD_S = 2.0
_MAX_ACCEPTABLE_BLOCK_S = 0.5


class _EnabledPlugin(_observability.ObservabilityPlugin):
    """Minimal registered+enabled plugin, so the per-call tracer path engages."""

    _stats_enabled = True
    _tracing_enabled = False

    def create_client_call_tracer(self, method_name, target):
        # Not a real capsule; grpc catches the resulting PyCapsule error. The
        # regression is the lock acquisition, which happens before this is hit.
        return object()

    def save_trace_context(self, trace_id, span_id, is_sampled):
        pass

    def create_server_call_tracer_factory(self, *, xds=False):
        return None

    def record_rpc_latency(self, method, target, rpc_latency, status_code):
        pass

    def save_registered_method(self, method_name):
        pass


class TestObservabilityDoesNotStallLoop(AioTestBase):
    def setUp(self):
        logging.getLogger("grpc").setLevel(logging.CRITICAL)
        _observability.set_plugin(_EnabledPlugin())

    def tearDown(self):
        _observability.set_plugin(None)
        logging.getLogger("grpc").setLevel(logging.NOTSET)

    async def _time_call_construction_under_held_lock(
        self, registered_method: bool
    ) -> float:
        """Time constructing one client call while another thread holds the lock."""
        channel = aio.insecure_channel("127.0.0.1:1")  # nothing listening; fine
        try:
            # Warm the channel first: the first call on a fresh channel defers
            # grpc_call creation, so the tracer-setup path only runs
            # synchronously at construction once the channel is ready.
            try:
                await asyncio.wait_for(
                    channel.unary_unary("/warm/Warm", lambda x: x, lambda x: x)(b"x"),
                    0.05,
                )
            except Exception:
                pass
            await asyncio.sleep(0.2)  # let the warm call settle so the channel is ready

            acquired = threading.Event()

            def hold_plugin_lock():
                with _observability._plugin_lock:
                    acquired.set()
                    time.sleep(_LOCK_HOLD_S)

            holder = threading.Thread(target=hold_plugin_lock, daemon=True)
            holder.start()
            self.assertTrue(acquired.wait(timeout=5.0), "lock holder never started")

            # Construct a real client call on the event loop thread while the
            # lock is held. Pre-fix this runs get_plugin() and blocks until the
            # holder releases; with the fix it reads the plugin lock-free.
            mc = channel.unary_unary(
                "/test/Method",
                lambda x: x,
                lambda x: x,
                _registered_method=registered_method,
            )
            start = time.monotonic()
            call = mc(b"payload")
            elapsed = time.monotonic() - start
            call.cancel()
            holder.join(timeout=5.0)
        finally:
            await channel.close()
        return elapsed

    def _assert_did_not_stall(self, elapsed: float) -> None:
        self.assertLess(
            elapsed,
            _MAX_ACCEPTABLE_BLOCK_S,
            f"aio client call construction blocked for {elapsed:.2f}s -- the "
            "event loop was stalled acquiring _plugin_lock while another thread "
            "held it",
        )

    async def test_client_call_construction_does_not_block_on_plugin_lock(self):
        # Exercises _maybe_set_client_call_tracer_on_call().
        self._assert_did_not_stall(
            await self._time_call_construction_under_held_lock(
                registered_method=False
            )
        )

    async def test_registered_method_call_does_not_block_on_plugin_lock(self):
        # Exercises _maybe_save_registered_method(), which generated stubs reach
        # on every RPC because registered methods are their default.
        self._assert_did_not_stall(
            await self._time_call_construction_under_held_lock(
                registered_method=True
            )
        )


if __name__ == "__main__":
    logging.basicConfig()
    unittest.main(verbosity=2)
