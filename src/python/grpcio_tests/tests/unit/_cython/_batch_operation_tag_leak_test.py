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

import gc
import logging
from pathlib import Path
import time
from typing import Optional
import unittest

import grpc
from grpc._cython import cygrpc

from tests.unit.framework.common import test_constants

_EMPTY_FLAGS = 0
_METHOD = "/test/Method"
_REQUEST = b"\x00\x00\x00"

# Binary (`-bin`) metadata values have to be `bytes`, so preparing an
# operation carrying a value of type `str()` fails.
_INVALID_METADATA = (("key-bin", "not bytes"),)
_VALID_METADATA = (("key", "value"),)

_NUM_ATTEMPTS = 10
_NUM_WARMUP_ATTEMPTS = 10
_NUM_LEAK_ATTEMPTS = 1000
# Leak caught here was ~7 KB per call. For 1000 leak attempts it gives ~7 MB.
# We use 2 MB as a safety margin.
_LEAK_THRESHOLD_BYTES = 2 * 1024 * 1024


def _count_live_objects(cls: type) -> int:
    gc.collect()
    # Compares types directly, as `isinstance()` may access `__class__` of
    # arbitrary objects (e.g. raising `ReferenceError` for dead weakproxies).
    # pylint: disable=unidiomatic-typecheck
    return sum(1 for obj in gc.get_objects() if type(obj) is cls)


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


def _duplicated_operations():
    # Core refuses batches with two operations of the same type synchronously
    # (`GRPC_CALL_ERROR_TOO_MANY_OPERATIONS`), without any side effect on the
    # call. Each operation owns C copies of its metadata once prepared.
    return (
        cygrpc.SendInitialMetadataOperation(_VALID_METADATA, _EMPTY_FLAGS),
        cygrpc.SendInitialMetadataOperation(_VALID_METADATA, _EMPTY_FLAGS),
    )


class _Rpc:
    """An RPC between a cygrpc server and channel, with control over batches."""

    def __init__(self):
        self._queue = cygrpc.CompletionQueue()
        self._server = cygrpc.Server(((b"grpc.so_reuseport", 0),), False)
        self._server.register_completion_queue(self._queue)
        port = self._server.add_http2_port(b"[::]:0")
        self._server.start()
        self._channel = cygrpc.Channel(f"localhost:{port}".encode(), (), None)
        self._server.request_call(self._queue, self._queue, None)
        operations = (
            cygrpc.SendInitialMetadataOperation(None, _EMPTY_FLAGS),
            cygrpc.SendCloseFromClientOperation(_EMPTY_FLAGS),
            cygrpc.ReceiveStatusOnClientOperation(_EMPTY_FLAGS),
        )
        self._client_call = self._channel.segregated_call(
            _EMPTY_FLAGS,
            _METHOD.encode(),
            None,
            None,
            None,
            None,
            ((operations, None),),
        )
        deadline = time.time() + test_constants.SHORT_TIMEOUT
        self.server_call = self._queue.poll(deadline=deadline).call

    def close(self):
        # Cancelling the RPC completes the client's batch locally, so it does
        # not depend on any I/O progress (nobody polls both completion queues).
        self._client_call.cancel(cygrpc.StatusCode.cancelled, "Test is over.")
        self._client_call.next_event()
        self.server_call = None
        self._channel.close(cygrpc.StatusCode.cancelled, "Test is over.")
        self._server.shutdown(self._queue, None)
        self._server.cancel_all_calls()
        deadline = time.time() + test_constants.SHORT_TIMEOUT
        while not isinstance(
            self._queue.poll(deadline=deadline), cygrpc.ServerShutdownEvent
        ):
            if deadline < time.time():
                raise AssertionError("The server did not shut down in time.")
        self._server.destroy()


class BatchOperationTagLeakTest(unittest.TestCase):
    def test_client_batch_rejected_by_core(self):
        # No server is needed. Core refuses the batch before any I/O.
        channel = cygrpc.Channel(b"localhost:1", (), None)
        baseline = _count_live_objects(cygrpc._BatchOperationTag)

        for _ in range(_NUM_ATTEMPTS):
            operation = cygrpc.ReceiveInitialMetadataOperation(_EMPTY_FLAGS)
            with self.assertRaises(ValueError):
                channel.segregated_call(
                    _EMPTY_FLAGS,
                    _METHOD.encode(),
                    None,
                    None,
                    None,
                    None,
                    (((operation,) + _duplicated_operations(), None),),
                )

            # Check if operation was released
            self.assertEqual((), operation.initial_metadata())

        self.assertEqual(
            baseline, _count_live_objects(cygrpc._BatchOperationTag)
        )
        channel.close(cygrpc.StatusCode.ok, "Test is over.")

    def test_client_batch_failing_to_prepare(self):
        channel = cygrpc.Channel(b"localhost:1", (), None)
        prepared_operation = cygrpc.ReceiveInitialMetadataOperation(
            _EMPTY_FLAGS
        )

        with self.assertRaises(TypeError):
            channel.segregated_call(
                _EMPTY_FLAGS,
                _METHOD.encode(),
                None,
                None,
                None,
                None,
                (
                    (
                        (
                            prepared_operation,
                            cygrpc.SendInitialMetadataOperation(
                                _INVALID_METADATA, _EMPTY_FLAGS
                            ),
                        ),
                        None,
                    ),
                ),
            )

        # Check if prepared operation was released
        self.assertEqual((), prepared_operation.initial_metadata())
        channel.close(cygrpc.StatusCode.ok, "Test is over.")

    def test_server_batch_rejected_by_core(self):
        rpc = _Rpc()
        baseline = _count_live_objects(cygrpc._BatchOperationTag)

        for _ in range(_NUM_ATTEMPTS):
            operation = cygrpc.ReceiveCloseOnServerOperation(_EMPTY_FLAGS)
            self.assertEqual(
                cygrpc.CallError.too_many_operations,
                rpc.server_call.start_server_batch(
                    (operation,) + _duplicated_operations(), None
                ),
            )

            # Check if operation was released
            self.assertIs(False, operation.cancelled())

        self.assertEqual(
            baseline, _count_live_objects(cygrpc._BatchOperationTag)
        )
        rpc.close()

    def test_server_batch_failing_to_prepare(self):
        rpc = _Rpc()
        prepared_operation = cygrpc.ReceiveCloseOnServerOperation(_EMPTY_FLAGS)

        with self.assertRaises(TypeError):
            rpc.server_call.start_server_batch(
                (
                    prepared_operation,
                    cygrpc.SendInitialMetadataOperation(
                        _INVALID_METADATA, _EMPTY_FLAGS
                    ),
                ),
                None,
            )

        # Check if prepared operation was released
        self.assertIs(False, prepared_operation.cancelled())
        rpc.close()

    def test_request_call_rejected_by_core(self):
        completion_queue = cygrpc.CompletionQueue()
        shut_down_completion_queue = cygrpc.CompletionQueue()
        server = cygrpc.Server(((b"grpc.so_reuseport", 0),), False)
        server.register_completion_queue(completion_queue)
        server.register_completion_queue(shut_down_completion_queue)
        server.add_http2_port(b"[::]:0")
        server.start()
        # Core refuses to request calls notified on a completion queue that is
        # shutting down (`GRPC_CALL_ERROR_COMPLETION_QUEUE_SHUTDOWN`).
        shut_down_completion_queue.shutdown()
        baseline = _count_live_objects(cygrpc._RequestCallTag)

        for _ in range(_NUM_ATTEMPTS):
            with self.assertRaisesRegex(
                cygrpc.InternalError, "grpc_server_request_call"
            ):
                server.request_call(
                    shut_down_completion_queue,
                    shut_down_completion_queue,
                    None,
                )

        self.assertEqual(baseline, _count_live_objects(cygrpc._RequestCallTag))
        server.shutdown(completion_queue, None)
        self.assertIsInstance(
            completion_queue.poll(
                deadline=time.time() + test_constants.SHORT_TIMEOUT
            ),
            cygrpc.ServerShutdownEvent,
        )
        server.destroy()

    @unittest.skipUnless(
        Path("/proc/self").is_dir(), "Requires `/proc/self` dir."
    )
    def test_call_released_when_its_batch_fails_to_prepare(self):
        metadata = _VALID_METADATA + _INVALID_METADATA
        with grpc.insecure_channel("localhost:1") as channel:
            multicallable = channel.unary_unary(
                _METHOD, _registered_method=True
            )

            self._invoke_callable(multicallable, metadata, _NUM_WARMUP_ATTEMPTS)
            gc.collect()
            before = _allocated_memory()
            self._invoke_callable(multicallable, metadata, _NUM_LEAK_ATTEMPTS)
            gc.collect()
            growth = _allocated_memory() - before

        self.assertLess(
            growth,
            _LEAK_THRESHOLD_BYTES,
            f"{growth} bytes leaked by {_NUM_LEAK_ATTEMPTS} batches",
        )

    def _invoke_callable(
        self,
        multicallable: grpc.UnaryUnaryMultiCallable,
        metadata: tuple,
        attempts: int,
    ):
        # The batches fail to prepare before reaching Core
        for _ in range(attempts):
            with self.assertRaises(TypeError):
                multicallable(_REQUEST, metadata=metadata)


if __name__ == "__main__":
    logging.basicConfig(level=logging.DEBUG)
    unittest.main(verbosity=2)
