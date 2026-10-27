# Copyright 2026 The gRPC Authors
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
"""Tests for the lifetime of the server-side `call` object (gRPC core)."""

import asyncio
import gc
import logging
import threading
import unittest
import weakref

import grpc
from grpc.experimental import aio

from tests.unit.framework.common import test_constants
from tests_aio.unit import _common
from tests_aio.unit._test_base import AioTestBase

# Time for the server to process a client cancellation and finish the RPC task.
_SETTLE_S = 0.5
_STREAM_LENGTH = 3


class _StatusCodeProbe:
    """Kept alive by the `RPCState` (via `ServicerContext.set_code()`).

    Used as a sentinel to determine when `RPCState` is deallocated.
    """
    def __init__(self, status_code=grpc.StatusCode.OK):
        self.value = status_code.value


def _park_probe(context):
    """Parks a `_StatusCodeProbe` on the `RPCState` behind `context`.

    Returns a weak reference to the probe, which dies together with the
    `RPCState`.
    """
    probe = _StatusCodeProbe()
    context.set_code(probe)
    return weakref.ref(probe)


def _abort_probe():
    probe = _StatusCodeProbe(grpc.StatusCode.ABORTED)
    return probe, weakref.ref(probe)


def _use_context(context, outcome):
    """Exercises a sync servicer `context` after the client cancelled.

    Records plain values only in `outcome`: an exception object would retain
    the handler frame (through its traceback) and so the servicer context.
    """
    try:
        outcome["peer"] = context.peer()
        outcome["time_remaining"] = context.time_remaining()
        outcome["invocation_metadata"] = context.invocation_metadata()
        try:
            # A Core batch on the cancelled call.
            context.send_initial_metadata((("late", "metadata"),))
            outcome["send_initial_metadata"] = "sent"
        except Exception as exc:  # pylint: disable=broad-except
            outcome["send_initial_metadata"] = type(exc).__name__
    except BaseException as exc:  # pylint: disable=broad-except
        outcome["error"] = repr(exc)


class ServerCallLifetimeTest(AioTestBase):
    async def setUp(self):
        # Only ref-counting may release the `RPCState` for this test suite.
        gc.disable()
        self.addCleanup(gc.enable)
        self._thread_pool = _common.DaemonThreadExecutor()
        self._server = aio.server(migration_thread_pool=self._thread_pool)
        self._adhoc_handlers = _common.AdhocGenericHandler()
        self._server.add_generic_rpc_handlers((self._adhoc_handlers,))
        port = self._server.add_insecure_port("[::]:0")
        await self._server.start()
        self._channel = aio.insecure_channel(f"localhost:{port}")

    async def tearDown(self):
        await self._channel.close()
        await self._server.stop(None)

    async def _in_thread(self, fn, *args):
        return await self.loop.run_in_executor(None, fn, *args)

    async def _assert_rpc_state_released(self, probe_ref):
        self.assertIsNone(
            probe_ref(), "Probe was never parked. The handler did not run"
        )
        deadline = self.loop.time() + test_constants.TIME_ALLOWANCE
        while probe_ref() is not None and self.loop.time() < deadline:
            await asyncio.sleep(0.01)
        self.assertIsNone(
            probe_ref(),
            "RPCState was not released by reference counting",
        )

    async def _assert_workers_finished(self):
        self.assertTrue(
            await self._in_thread(
                self._thread_pool.workers_finished,
                test_constants.TIME_ALLOWANCE,
            ),
            "thread pool worker leaked",
        )

    async def test_released_after_async_unary_unary_rpc(self):
        probe_ref = None

        @grpc.unary_unary_rpc_method_handler
        async def unary_unary_handler(unused_request, context):
            nonlocal probe_ref
            probe_ref = _park_probe(context)
            return b""

        self._adhoc_handlers.set_adhoc_handler(unary_unary_handler)
        multi_callable = self._channel.unary_unary(_common.ADHOC_METHOD)
        await multi_callable(b"")
        await self._assert_rpc_state_released(probe_ref)

    async def test_released_after_async_unary_stream_rpc(self):
        probe_ref = None

        @grpc.unary_stream_rpc_method_handler
        async def unary_stream_handler(unused_request, context):
            nonlocal probe_ref
            probe_ref = _park_probe(context)
            for _ in range(_STREAM_LENGTH):
                yield b""

        self._adhoc_handlers.set_adhoc_handler(unary_stream_handler)
        multi_callable = self._channel.unary_stream(_common.ADHOC_METHOD)
        call = multi_callable(b"")
        async for _ in call:
            pass
        await self._assert_rpc_state_released(probe_ref)

    async def test_released_after_async_stream_unary_rpc(self):
        probe_ref = None

        @grpc.stream_unary_rpc_method_handler
        async def stream_unary_handler(request_iterator, context):
            nonlocal probe_ref
            probe_ref = _park_probe(context)
            async for _ in request_iterator:
                pass
            return b""

        self._adhoc_handlers.set_adhoc_handler(stream_unary_handler)
        multi_callable = self._channel.stream_unary(_common.ADHOC_METHOD)
        await multi_callable([b""] * _STREAM_LENGTH)
        await self._assert_rpc_state_released(probe_ref)

    async def test_released_after_async_stream_stream_rpc(self):
        probe_ref = None

        @grpc.stream_stream_rpc_method_handler
        async def stream_stream_handler(request_iterator, context):
            nonlocal probe_ref
            probe_ref = _park_probe(context)
            async for request in request_iterator:
                yield request

        self._adhoc_handlers.set_adhoc_handler(stream_stream_handler)
        multi_callable = self._channel.stream_stream(_common.ADHOC_METHOD)
        call = multi_callable([b""] * _STREAM_LENGTH)
        async for _ in call:
            pass
        await self._assert_rpc_state_released(probe_ref)

    async def test_released_after_sync_unary_unary_rpc(self):
        probe_ref = None

        @grpc.unary_unary_rpc_method_handler
        def unary_unary_handler(unused_request, context):
            nonlocal probe_ref
            probe_ref = _park_probe(context)
            return b""

        self._adhoc_handlers.set_adhoc_handler(unary_unary_handler)
        multi_callable = self._channel.unary_unary(_common.ADHOC_METHOD)
        await multi_callable(b"")
        await self._assert_workers_finished()
        await self._assert_rpc_state_released(probe_ref)

    async def test_released_after_sync_unary_stream_rpc(self):
        probe_ref = None

        @grpc.unary_stream_rpc_method_handler
        def unary_stream_handler(unused_request, context):
            nonlocal probe_ref
            probe_ref = _park_probe(context)
            for _ in range(_STREAM_LENGTH):
                yield b""

        self._adhoc_handlers.set_adhoc_handler(unary_stream_handler)
        multi_callable = self._channel.unary_stream(_common.ADHOC_METHOD)
        call = multi_callable(b"")
        async for _ in call:
            pass
        await self._assert_workers_finished()
        await self._assert_rpc_state_released(probe_ref)

    async def test_released_after_sync_stream_unary_rpc(self):
        probe_ref = None

        @grpc.stream_unary_rpc_method_handler
        def stream_unary_handler(request_iterator, context):
            nonlocal probe_ref
            probe_ref = _park_probe(context)
            for _ in request_iterator:
                pass
            return b""

        self._adhoc_handlers.set_adhoc_handler(stream_unary_handler)
        multi_callable = self._channel.stream_unary(_common.ADHOC_METHOD)
        await multi_callable([b""] * _STREAM_LENGTH)
        await self._assert_workers_finished()
        await self._assert_rpc_state_released(probe_ref)

    async def test_released_after_sync_stream_stream_rpc(self):
        probe_ref = None

        @grpc.stream_stream_rpc_method_handler
        def stream_stream_handler(request_iterator, context):
            nonlocal probe_ref
            probe_ref = _park_probe(context)
            for request in request_iterator:
                yield request

        self._adhoc_handlers.set_adhoc_handler(stream_stream_handler)
        multi_callable = self._channel.stream_stream(_common.ADHOC_METHOD)
        call = multi_callable([b""] * _STREAM_LENGTH)
        async for _ in call:
            pass
        await self._assert_workers_finished()
        await self._assert_rpc_state_released(probe_ref)

    async def test_released_after_async_rpc_with_done_callback(self):
        callback_ran = asyncio.Event()
        probe_ref = None

        @grpc.unary_unary_rpc_method_handler
        async def unary_unary_handler(unused_request, context):
            nonlocal probe_ref
            probe_ref = _park_probe(context)
            context.add_done_callback(lambda unused_context: callback_ran.set())
            return b""

        self._adhoc_handlers.set_adhoc_handler(unary_unary_handler)
        multi_callable = self._channel.unary_unary(_common.ADHOC_METHOD)
        await multi_callable(b"")
        await asyncio.wait_for(
            callback_ran.wait(), test_constants.TIME_ALLOWANCE
        )
        await self._assert_rpc_state_released(probe_ref)

    async def test_released_after_sync_rpc_with_done_callback(self):
        outcome = {}
        probe_ref = None

        @grpc.unary_unary_rpc_method_handler
        def unary_unary_handler(unused_request, context):
            nonlocal probe_ref
            probe_ref = _park_probe(context)
            outcome["add_callback"] = context.add_callback(
                lambda: outcome.update(callback_peer=context.peer())
            )
            return b""

        self._adhoc_handlers.set_adhoc_handler(unary_unary_handler)
        multi_callable = self._channel.unary_unary(_common.ADHOC_METHOD)
        await multi_callable(b"")
        await self._assert_workers_finished()
        self.assertIs(outcome.get("add_callback"), True, outcome)
        self.assertTrue(outcome.get("callback_peer"), outcome)
        await self._assert_rpc_state_released(probe_ref)

    async def test_released_after_async_aborted_rpc(self):
        probe_ref = None

        @grpc.unary_unary_rpc_method_handler
        async def aborted_unary_unary_handler(unused_request, context):
            nonlocal probe_ref
            probe, probe_ref = _abort_probe()
            await context.abort(probe, "aborted")

        self._adhoc_handlers.set_adhoc_handler(aborted_unary_unary_handler)
        multi_callable = self._channel.unary_unary(_common.ADHOC_METHOD)
        with self.assertRaises(aio.AioRpcError) as exception_context:
            await multi_callable(b"")
        self.assertEqual(
            grpc.StatusCode.ABORTED, exception_context.exception.code()
        )
        await self._assert_rpc_state_released(probe_ref)

    async def test_released_after_sync_aborted_rpc(self):
        probe_ref = None

        @grpc.unary_unary_rpc_method_handler
        def aborted_unary_unary_handler(unused_request, context):
            nonlocal probe_ref
            probe, probe_ref = _abort_probe()
            context.abort(probe, "aborted")

        self._adhoc_handlers.set_adhoc_handler(aborted_unary_unary_handler)
        multi_callable = self._channel.unary_unary(_common.ADHOC_METHOD)
        with self.assertRaises(aio.AioRpcError) as exception_context:
            await multi_callable(b"")
        self.assertEqual(
            grpc.StatusCode.ABORTED, exception_context.exception.code()
        )
        await self._assert_workers_finished()
        await self._assert_rpc_state_released(probe_ref)

    async def test_released_when_handler_stops_reading_early(self):
        probe_ref = None

        @grpc.stream_unary_rpc_method_handler
        async def early_exit_stream_unary_handler(request_iterator, context):
            nonlocal probe_ref
            probe_ref = _park_probe(context)
            await request_iterator.__anext__()  # one of two messages
            return b""

        self._adhoc_handlers.set_adhoc_handler(early_exit_stream_unary_handler)
        multi_callable = self._channel.stream_unary(_common.ADHOC_METHOD)
        call = multi_callable()
        await call.write(b"")
        await call.write(b"")
        await call.done_writing()
        await call
        await self._assert_rpc_state_released(probe_ref)

    async def test_sync_unary_handler_outliving_client_cancel(self):
        handler_started = threading.Event()
        release = threading.Event()
        outcome = {}
        probe_ref = None

        @grpc.unary_unary_rpc_method_handler
        def unary_unary_handler(unused_request, context):
            nonlocal probe_ref
            probe_ref = _park_probe(context)
            handler_started.set()
            release.wait(test_constants.TIME_ALLOWANCE)
            _use_context(context, outcome)
            outcome["add_callback"] = context.add_callback(
                lambda: outcome.update(late_callback=context.peer())
            )
            return b""

        self._adhoc_handlers.set_adhoc_handler(unary_unary_handler)
        multi_callable = self._channel.unary_unary(_common.ADHOC_METHOD)
        call = multi_callable(b"")
        self.assertTrue(
            await self._in_thread(
                handler_started.wait, test_constants.TIME_ALLOWANCE
            )
        )

        call.cancel()
        await asyncio.sleep(_SETTLE_S)
        self.assertIsNotNone(
            probe_ref(), "RPCState released while the handler still runs"
        )

        release.set()
        await self._assert_workers_finished()
        self.assertNotIn("error", outcome)
        self.assertTrue(outcome.get("peer"), f'Expected "peer" in {outcome}')
        self.assertIn("send_initial_metadata", outcome)
        self.assertIs(outcome.get("add_callback"), False, outcome)
        self.assertNotIn("late_callback", outcome)
        await self._assert_rpc_state_released(probe_ref)

    async def test_sync_stream_handler_outliving_client_cancel(self):
        release = threading.Event()
        outcome = {}
        probe_ref = None

        @grpc.unary_stream_rpc_method_handler
        def unary_stream_handler(unused_request, context):
            nonlocal probe_ref
            probe_ref = _park_probe(context)
            yield b"first"
            release.wait(test_constants.TIME_ALLOWANCE)
            _use_context(context, outcome)

        self._adhoc_handlers.set_adhoc_handler(unary_stream_handler)
        multi_callable = self._channel.unary_stream(_common.ADHOC_METHOD)
        call = multi_callable(b"")
        self.assertEqual(b"first", await call.read())

        call.cancel()
        await asyncio.sleep(_SETTLE_S)
        self.assertIsNotNone(
            probe_ref(), "RPCState released while the handler still runs"
        )

        release.set()
        await self._assert_workers_finished()
        self.assertNotIn("error", outcome)
        self.assertTrue(outcome.get("peer"), f'Expected "peer" in {outcome}')
        self.assertIn("send_initial_metadata", outcome)
        await self._assert_rpc_state_released(probe_ref)

    async def test_sync_handler_abort_after_client_cancel(self):
        handler_started = threading.Event()
        release = threading.Event()
        outcome = {}

        @grpc.unary_unary_rpc_method_handler
        def unary_unary_handler(unused_request, context):
            handler_started.set()
            release.wait(test_constants.TIME_ALLOWANCE)
            try:
                context.abort(grpc.StatusCode.ABORTED, "too late")
            except Exception as exc:  # pylint: disable=broad-except
                outcome["abort"] = type(exc).__name__
            return b""

        self._adhoc_handlers.set_adhoc_handler(unary_unary_handler)
        multi_callable = self._channel.unary_unary(_common.ADHOC_METHOD)
        call = multi_callable(b"")
        self.assertTrue(
            await self._in_thread(
                handler_started.wait, test_constants.TIME_ALLOWANCE
            )
        )
        call.cancel()
        await asyncio.sleep(_SETTLE_S)
        release.set()
        await self._assert_workers_finished()
        # Aborting a cancelled RPC fails, but with an exception - not a crash.
        self.assertIn("abort", outcome, outcome)


if __name__ == "__main__":
    logging.basicConfig(level=logging.DEBUG)
    unittest.main(verbosity=2)
