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
"""Tests for the finalizers of abandoned client calls."""

import asyncio
import gc
import logging
import threading
import unittest

from grpc._cython import cygrpc
from grpc.aio._call import Call
from grpc.aio._interceptor import InterceptedUnaryUnaryCall
from grpc.experimental import aio

from tests.unit.framework.common import test_constants
from tests_aio.unit._test_base import AioTestBase

_UNREACHABLE_TARGET = "localhost:1"
_UNREACHABLE_METHOD = "/test/Unreachable"


class _PassThroughInterceptor(aio.UnaryUnaryClientInterceptor):
    async def intercept_unary_unary(
        self, continuation, client_call_details, request
    ):
        return await continuation(client_call_details, request)


class TestCallFinalizer(AioTestBase):
    async def test_finalizer_cancels_on_loop_thread(self):
        loop_thread = threading.get_ident()
        callback_threads = []
        called = asyncio.Event()

        def callback(unused_call):
            callback_threads.append(threading.get_ident())
            self.loop.call_soon_threadsafe(called.set)

        channel = cygrpc.AioChannel(
            _UNREACHABLE_TARGET.encode(), (), None, self.loop
        )
        gc.disable()
        try:
            call = Call(
                channel.call(
                    _UNREACHABLE_METHOD.encode(), None, None, None, None
                ),
                aio.Metadata(),
                None,
                None,
                self.loop,
            )
            call.add_done_callback(callback)
            del call
            # simulating GC running on a loop's non-owning thread
            await asyncio.to_thread(gc.collect)
        finally:
            gc.enable()

        await asyncio.wait_for(called.wait(), test_constants.SHORT_TIMEOUT)
        self.assertEqual([loop_thread], callback_threads)
        channel.close()


class TestInterceptedUnaryUnaryCallFinalizer(AioTestBase):
    async def test_finalizer_cancels_on_loop_thread(self):
        loop_thread = threading.get_ident()
        callback_threads = []
        called = asyncio.Event()

        def callback(unused_call):
            callback_threads.append(threading.get_ident())
            self.loop.call_soon_threadsafe(called.set)

        channel = cygrpc.AioChannel(
            _UNREACHABLE_TARGET.encode(), (), None, self.loop
        )
        intercepted_call = InterceptedUnaryUnaryCall(
            [_PassThroughInterceptor()],
            b"",
            None,
            aio.Metadata(),
            None,
            True,  # wait_for_ready; nothing listens on the target
            channel,
            _UNREACHABLE_METHOD.encode(),
            None,
            None,
            self.loop,
        )
        call = await intercepted_call._interceptors_task
        self.assertFalse(call.done())
        call.add_done_callback(callback)
        # only a reference cycle keeps the interceptor wrapper alive
        intercepted_call._cycle = intercepted_call

        gc.disable()
        try:
            del intercepted_call
            # simulating GC running on a loop's non-owning thread
            await asyncio.to_thread(gc.collect)
        finally:
            gc.enable()

        await asyncio.wait_for(called.wait(), test_constants.SHORT_TIMEOUT)
        self.assertEqual([loop_thread], callback_threads)
        self.assertTrue(call.cancelled())
        channel.close()


if __name__ == "__main__":
    logging.basicConfig(level=logging.DEBUG)
    unittest.main(verbosity=2)
