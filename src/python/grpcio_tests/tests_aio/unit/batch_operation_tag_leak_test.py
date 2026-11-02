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
"""Tests that batches release the C copies of their operations exactly once."""

import asyncio
import gc
import logging
import os
import time
from typing import Optional
import unittest

import grpc
from grpc._cython import cygrpc
from grpc.experimental import aio

from tests.unit.framework.common import test_constants
from tests_aio.unit._test_base import AioTestBase

_EMPTY_FLAGS = 0
_ECHO = "/test/Echo"
_REQUEST = b"\x00\x00\x00"
_POLL_INTERVAL_S = 0.05

# Binary (`-bin`) metadata values have to be `bytes`, so preparing an
# operation carrying a value of type `str()` fails.
_INVALID_METADATA = (("key-bin", "not bytes"),)
# The value of each entry is too long to be inlined in a slice, so preparing an
# operation carrying this metadata allocates C copies of all the values.
_VALID_METADATA = tuple((f"key-{index}", "v" * 100) for index in range(1000))

_NUM_WARMUP_ATTEMPTS = 10
_NUM_LEAK_ATTEMPTS = 100
# Smallest leak caught here was ~96 KB (metadata array). For 100 leak attempts
# it gives ~9 MB. We use 2 MB as a safety margin.
_LEAK_THRESHOLD_BYTES = 2 * 1024 * 1024


def _count_live_batch_operation_tags(operations: tuple) -> int:
    gc.collect()
    # Compares types directly, as `isinstance()` may access `__class__` of
    # arbitrary objects (e.g. raising `ReferenceError` for dead weakproxies).
    return sum(
        1
        for obj in gc.get_objects()
        if type(obj) is cygrpc._BatchOperationTag
        and obj._operations is operations
    )


def _allocated_memory() -> Optional[int]:
    """Returns the memory allocated by this process in bytes, or `None`.

    This is the `VmData` field of `/proc/self/status` (Linux only), written in
    the format: "VmData:  123456 kB". It represents the size of the heap and of
    the other private writable mappings. Unlike RSS, it also counts memory that
    was allocated but never written to.

    This function is needed due to the C constructs used by the
    `_BatchOperationTag.prepare()` method.
    """
    try:
        with open("/proc/self/status") as status:
            for line in status:
                if line.startswith("VmData:"):
                    return int(line.split()[1]) * 1024
    except OSError:
        pass
    return None


class _EchoHandler(grpc.GenericRpcHandler):
    async def _echo(self, request_iterator, unused_context):
        async for request in request_iterator:
            yield request

    def service(self, handler_call_details):
        if handler_call_details.method != _ECHO:
            return None
        return grpc.stream_stream_rpc_method_handler(self._echo)


class TestBatchOperationTagLeak(AioTestBase):
    async def test_batch_rejected_by_core(self):
        operation = cygrpc.ReceiveInitialMetadataOperation(_EMPTY_FLAGS)

        # Without an underlying `grpc_call`, Core refuses every batch
        # synchronously (`GRPC_CALL_ERROR`).
        with self.assertRaises(cygrpc.ExecuteBatchError):
            await cygrpc.execute_batch(
                cygrpc.GrpcCallWrapper(), (operation,), self.loop
            )

        # Check if operation was released
        self.assertEqual((), operation.initial_metadata())

    async def test_batch_failing_to_prepare(self):
        prepared_operation = cygrpc.ReceiveInitialMetadataOperation(
            _EMPTY_FLAGS
        )

        with self.assertRaises(TypeError):
            await cygrpc.execute_batch(
                cygrpc.GrpcCallWrapper(),
                (
                    prepared_operation,
                    cygrpc.SendInitialMetadataOperation(
                        _INVALID_METADATA, _EMPTY_FLAGS
                    ),
                ),
                self.loop,
            )

        # Check if prepared operation was released
        self.assertEqual((), prepared_operation.initial_metadata())

    async def test_cancelled_batch_released_once_completed_by_core(self):
        server = aio.server(handlers=(_EchoHandler(),))
        port = server.add_insecure_port("[::]:0")
        await server.start()
        try:
            async with aio.insecure_channel(f"localhost:{port}") as channel:
                call = channel.stream_stream(_ECHO)()
                await call.wait_for_connection()

                # Starts a batch directly on the underlying call, so that its
                # operation can be inspected
                operations = (cygrpc.ReceiveMessageOperation(_EMPTY_FLAGS),)
                batch = self.loop.create_task(
                    cygrpc.execute_batch(
                        call._cython_call, operations, self.loop
                    )
                )
                # Pass over to Core to pick up the batch
                await asyncio.sleep(0)

                batch.cancel()
                with self.assertRaises(asyncio.CancelledError):
                    await batch

                # Core still owns the batch
                self.assertEqual(
                    1, _count_live_batch_operation_tags(operations)
                )
                self.assertIsNone(operations[0].message())

                # This request completes the batch
                await call.write(_REQUEST)
                deadline = time.monotonic() + test_constants.SHORT_TIMEOUT
                while (
                    operations[0].message() is None
                    and time.monotonic() < deadline
                ):
                    await asyncio.sleep(_POLL_INTERVAL_S)

                # Check if operation was released
                self.assertEqual(_REQUEST, operations[0].message())
                self.assertEqual(
                    0, _count_live_batch_operation_tags(operations)
                )

                await call.done_writing()
                self.assertEqual(grpc.StatusCode.OK, await call.code())
        finally:
            await server.stop(None)

    @unittest.skipUnless(
        os.path.isdir("/proc/self"), "Requires `/proc/self` dir."
    )
    async def test_metadata_failing_conversion(self):
        for invalid_position, metadata in (
            ("first", _INVALID_METADATA + _VALID_METADATA),
            ("last", _VALID_METADATA + _INVALID_METADATA),
        ):
            with self.subTest(invalid_position=invalid_position):
                await self._prepare_batches(metadata, _NUM_WARMUP_ATTEMPTS)
                gc.collect()
                before = _allocated_memory()
                await self._prepare_batches(metadata, _NUM_LEAK_ATTEMPTS)
                gc.collect()
                growth = _allocated_memory() - before

                self.assertLess(
                    growth,
                    _LEAK_THRESHOLD_BYTES,
                    f"{growth} bytes leaked by {_NUM_LEAK_ATTEMPTS} batches",
                )

    async def _prepare_batches(self, metadata: tuple, attempts: int):
        # The batches fail to prepare before reaching Core
        for _ in range(attempts):
            with self.assertRaises(TypeError):
                await cygrpc.execute_batch(
                    cygrpc.GrpcCallWrapper(),
                    (
                        cygrpc.SendInitialMetadataOperation(
                            metadata, _EMPTY_FLAGS
                        ),
                    ),
                    self.loop,
                )


if __name__ == "__main__":
    logging.basicConfig(level=logging.DEBUG)
    unittest.main(verbosity=2)
