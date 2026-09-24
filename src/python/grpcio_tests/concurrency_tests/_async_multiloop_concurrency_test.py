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
"""Multi-loop / multi-OS-thread freethreading Python tests for grpc aio core."""

import asyncio
import contextlib
import logging
import os
import unittest

import grpc
from grpc.experimental import aio

from concurrency_tests._concurrency_base import ConcurrencyTestCase

_THREADS = int(os.environ.get("GRPC_FT_MULTILOOP_THREADS", "16"))
_ITERATIONS = int(os.environ.get("GRPC_FT_MULTILOOP_ITERATIONS", "20"))
_HEAVY_ITERATIONS = int(
    os.environ.get(
        "GRPC_FT_MULTILOOP_HEAVY_ITERATIONS", str(max(2, _ITERATIONS // 4))
    )
)

_UNARY_UNARY = "/test/UnaryUnary"
_UNARY_STREAM = "/test/UnaryStream"
_STREAM_UNARY = "/test/StreamUnary"
_STREAM_STREAM = "/test/StreamStream"
_SLOW_UNARY = "/test/SlowUnary"


_REQUEST = b"\x00\x01\x02\x03"
_RESPONSE = b"\x03\x02\x01\x00"

_STREAM_LEN = 4
_INFLIGHT = 4
_INFLIGHT_SETTLE = 0.05
_DEADLINE = 0.1
_SLOW_HANDLER_SLEEP = 20


async def _unary_unary_handler(unused_request, unused_context):
    return _RESPONSE


async def _unary_stream_handler(unused_request, unused_context):
    for _ in range(_STREAM_LEN):
        yield _RESPONSE


async def _stream_unary_handler(request_iterator, unused_context):
    async for _ in request_iterator:
        pass
    return _RESPONSE


async def _stream_stream_handler(request_iterator, unused_context):
    async for _ in request_iterator:
        yield _RESPONSE


async def _slow_unary_unary_handler(unused_request, unused_context):
    await asyncio.sleep(_SLOW_HANDLER_SLEEP)
    return _RESPONSE


_HANDLERS = {
    _UNARY_UNARY: grpc.unary_unary_rpc_method_handler(_unary_unary_handler),
    _UNARY_STREAM: grpc.unary_stream_rpc_method_handler(_unary_stream_handler),
    _STREAM_UNARY: grpc.stream_unary_rpc_method_handler(_stream_unary_handler),
    _STREAM_STREAM: grpc.stream_stream_rpc_method_handler(
        _stream_stream_handler
    ),
    _SLOW_UNARY: grpc.unary_unary_rpc_method_handler(_slow_unary_unary_handler),
}


class _GenericHandler(grpc.GenericRpcHandler):
    def service(self, handler_call_details):
        return _HANDLERS.get(handler_call_details.method)


def _check_unary_response(response):
    if response != _RESPONSE:
        raise AssertionError(f"response mismatch {response} != {_RESPONSE}")


def _check_stream_response(responses):
    expected = [_RESPONSE] * _STREAM_LEN
    if responses != expected:
        raise AssertionError(f"responses mismatch {responses} != {expected}")


async def _request_iterator():
    for _ in range(_STREAM_LEN):
        yield _REQUEST


async def _perform_unary_unary(channel):
    _check_unary_response(await channel.unary_unary(_UNARY_UNARY)(_REQUEST))


async def _perform_unary_stream(channel):
    call = channel.unary_stream(_UNARY_STREAM)(_REQUEST)
    _check_stream_response([response async for response in call])


async def _perform_stream_unary(channel):
    _check_unary_response(
        await channel.stream_unary(_STREAM_UNARY)(_request_iterator())
    )


async def _perform_stream_stream(channel):
    call = channel.stream_stream(_STREAM_STREAM)(_request_iterator())
    _check_stream_response([response async for response in call])


_RPC_EXECUTORS = (
    _perform_unary_unary,
    _perform_unary_stream,
    _perform_stream_unary,
    _perform_stream_stream,
)

async def _start_server_and_channel():
    server = aio.server()
    server.add_generic_rpc_handlers((_GenericHandler(),))
    port = server.add_insecure_port("localhost:0")
    await server.start()
    channel = aio.insecure_channel(f"localhost:{port}")
    return server, channel


@contextlib.asynccontextmanager
async def _server_and_channel():
    server, channel = await _start_server_and_channel()
    try:
        yield channel
    finally:
        await channel.close()
        await server.stop(None)


async def _scenario_unary_unary(iterations):
    """Unary-unary RPC call x iterations"""
    async with _server_and_channel() as channel:
        for _ in range(iterations):
            await _perform_unary_unary(channel)


async def _scenario_all_rpcs(iterations):
    """All four RPC calls gathered concurrently per iteration"""
    async with _server_and_channel() as channel:
        for _ in range(iterations):
            await asyncio.gather(
                *(rpc_executor(channel) for rpc_executor in _RPC_EXECUTORS)
            )


async def _scenario_deadline(iterations):
    """Short deadline vs slow handler -> DEADLINE_EXCEEDED"""
    async with _server_and_channel() as channel:
        # warm-up
        await _perform_unary_unary(channel)
        multicallable = channel.unary_unary(_SLOW_UNARY)
        for _ in range(iterations):
            try:
                await multicallable(_REQUEST, timeout=_DEADLINE)
            except grpc.aio.AioRpcError as exc:
                if exc.code() is not grpc.StatusCode.DEADLINE_EXCEEDED:
                    raise
            else:
                raise AssertionError("Expected DEADLINE_EXCEEDED")


async def _scenario_connectivity(iterations):
    """Connectivity watch (CallbackWrapper path)"""
    for _ in range(iterations):
        # recreate channel each iteration
        async with _server_and_channel() as channel:
            await channel.channel_ready()
            await _perform_unary_unary(channel)


async def _await_cancelled(call):
    """Returns None if the call was cancelled as expected, else the anomaly"""
    try:
        await call
    except asyncio.CancelledError:
        return None
    except grpc.aio.AioRpcError as exc:
        # when stream RPC is closed it can return UNAVAILABLE status as well
        if exc.code in (grpc.StatusCode.CANCELLED, grpc.StatusCode.UNAVAILABLE):
            return None
        return exc
    except Exception as exc:
        return exc
    return AssertionError("in-flight call completed despite close(None)")


async def _scenario_close_mid_rpc(iterations):
    """channel.close(None) with slow calls in-flight -> all CANCELLED"""
    for _ in range(iterations):
        server, channel = await _start_server_and_channel()
        try:
            await _perform_unary_unary(channel)
            calls = [
                channel.unary_unary(_SLOW_UNARY)(_REQUEST)
                for _ in range(_INFLIGHT)
            ]
            await asyncio.sleep(_INFLIGHT_SETTLE)
            await channel.close(None)
            outcomes = await asyncio.gather(
                *(_await_cancelled(call) for call in calls)
            )
            bad = [repr(o) for o in outcomes if o is not None]
            if bad:
                raise AssertionError(
                    f"close(None) left in-flight calls: {"; ".join(bad)}"
                )
        finally:
            await server.stop(None)


async def _scenario_aiostate_stress(iterations):
    """Repeated server_channel create/destroy -> _AioState refcount stress"""
    for _ in range(iterations):
        server, channel = await _start_server_and_channel()
        await _perform_unary_unary(channel)
        await channel.close()
        await server.stop(None)
        del server, channel


def _run_scenario_on_new_loop(scenario, iterations):
    """Run one scenario coroutine on a fresh loop owned by this thread"""
    loop = asyncio.new_event_loop()
    asyncio.set_event_loop(loop)
    try:
        loop.run_until_complete(scenario(iterations))
    finally:
        # the below does not influence scenario correctnes; it rather filter out
        # noise in the logs we have
        try:
            # when the scenario's channel.close / server.stop(None) cancel RPCs
            # the cancellation is scheduled but the server-side handler tasks
            # haven't run their CancellerError to completion yet
            for _ in range(5):
                loop.run_until_complete(asyncio.sleep(0))
        except Exception:
            pass
        try:
            # Streaming scenarios leave async generators, close them cleanly
            loop.run_until_complete(loop.shutdown_asyncgens())
        except Exception:
            pass
        try:
            loop.close()
        except Exception:
            pass


class AsyncMultiloopConcurrencyTest(ConcurrencyTestCase):
    thread_count = _THREADS

    def _run_multiloop(self, scenario, iterations):
        self.spawn_workers(
            lambda index: _run_scenario_on_new_loop(scenario, iterations),
        )

    def test_multiloop_unary_unary(self):
        self._run_multiloop(_scenario_unary_unary, _ITERATIONS)

    def test_multiloop_all_rpcs(self):
        self._run_multiloop(_scenario_all_rpcs, _ITERATIONS)

    def test_multiloop_deadline_expiry(self):
        self._run_multiloop(_scenario_deadline, _ITERATIONS)

    def test_multiloop_connectivity(self):
        self._run_multiloop(_scenario_connectivity, _HEAVY_ITERATIONS)

    def test_multiloop_close_mid_rpc(self):
        self._run_multiloop(_scenario_close_mid_rpc, _HEAVY_ITERATIONS)

    def test_multiloop_aiostate_stress(self):
        self._run_multiloop(_scenario_aiostate_stress, _HEAVY_ITERATIONS)


if __name__ == "__main__":
    logging.basicConfig()
    unittest.main(verbosity=2)
