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
"""Tests that CallbackWrapper objects do not outlive their Core operations."""

import asyncio
import gc
import logging
import unittest

import grpc
from grpc._cython import cygrpc
from grpc.experimental import aio

from tests_aio.unit._test_base import AioTestBase

_SEND_INITIAL_METADATA_TWICE = "/test/SendInitialMetadataTwice"

_REQUEST = b"\x00\x00\x00"
_RESPONSE = b"\x01\x01\x01"
_INITIAL_METADATA = (("initial-md-key", "initial-md-value"),)

_NUM_ATTEMPTS = 10


def _count_live_callback_wrappers() -> int:
    gc.collect()
    # Compares types directly, as isinstance() may access __class__ of
    # arbitrary objects (e.g. raising ReferenceError for dead weakproxies).
    return sum(
        1 for obj in gc.get_objects() if type(obj) is cygrpc.CallbackWrapper
    )


class _SendInitialMetadataTwiceHandler(grpc.GenericRpcHandler):
    """Races two send_initial_metadata() calls within the same RPC.

    The second batch is started before the first one completes, so Core
    refuses it synchronously (GRPC_CALL_ERROR_TOO_MANY_OPERATIONS).
    """

    def __init__(self):
        # Messages of the errors raised by send_initial_metadata(), per RPC.
        # The exceptions themselves are not stored, as their tracebacks could
        # extend the lifetime of the objects under test.
        self.errors = []

    async def _send_initial_metadata_twice(self, unused_request, context):
        outcomes = await asyncio.gather(
            context.send_initial_metadata(_INITIAL_METADATA),
            context.send_initial_metadata(_INITIAL_METADATA),
            return_exceptions=True,
        )
        self.errors.append(
            [str(outcome) for outcome in outcomes if outcome is not None]
        )
        return _RESPONSE

    def service(self, handler_call_details):
        if handler_call_details.method != _SEND_INITIAL_METADATA_TWICE:
            return None
        return grpc.unary_unary_rpc_method_handler(
            self._send_initial_metadata_twice
        )


class TestCallbackWrapperLeak(AioTestBase):
    def _assert_no_callback_wrapper_leaked(self, baseline: int):
        live = _count_live_callback_wrappers()
        self.assertLessEqual(
            live, baseline, f"{live - baseline} CallbackWrapper(s) leaked"
        )

    async def test_send_initial_metadata_rejected_by_core(self):
        baseline = _count_live_callback_wrappers()

        handler = _SendInitialMetadataTwiceHandler()
        server = aio.server(handlers=(handler,))
        port = server.add_insecure_port("[::]:0")
        await server.start()
        async with aio.insecure_channel(f"localhost:{port}") as channel:
            multicallable = channel.unary_unary(_SEND_INITIAL_METADATA_TWICE)
            for _ in range(_NUM_ATTEMPTS):
                self.assertEqual(_RESPONSE, await multicallable(_REQUEST))

            # Sanity check
            self.assertGreater(_count_live_callback_wrappers(), baseline)
        await server.stop(None)

        # Each RPC went through the synchronous failure path exactly once.
        self.assertEqual(_NUM_ATTEMPTS, len(handler.errors))
        for errors in handler.errors:
            self.assertEqual(1, len(errors))
            self.assertIn("grpc_call_start_batch", errors[0])

        self._assert_no_callback_wrapper_leaked(baseline)


if __name__ == "__main__":
    logging.basicConfig(level=logging.DEBUG)
    unittest.main(verbosity=2)
