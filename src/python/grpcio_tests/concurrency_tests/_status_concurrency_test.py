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
"""Concurrency tests of grpc_status."""

import logging
import traceback
import unittest

import grpc
from grpc_status import rpc_status

from google.protobuf import any_pb2
from google.rpc import code_pb2
from google.rpc import error_details_pb2
from google.rpc import status_pb2

from concurrency_tests._concurrency_base import ITERATIONS_PER_THREAD
from concurrency_tests._concurrency_base import ConcurrencyTestCase

_STATUS_OK = "/test/StatusOK"
_ERROR_DETAILS = "/test/ErrorDetails"
_REQUEST = b"\x00\x00\x00"
_RESPONSE = b"\x01\x01\x01"
_STATUS_DETAILS = "This is an error detail"

def _ok_unary_unary(request, servicer_context):
    return _RESPONSE


def _error_details_unary_unary(request, servicer_context):
    details = any_pb2.Any()
    details.Pack(
        error_details_pb2.DebugInfo(
            stack_entries=traceback.format_stack(),
            detail="Intentionally invoked",
        )
    )
    rich_status = status_pb2.Status(
        code=code_pb2.INTERNAL,
        message=_STATUS_DETAILS,
        details=[details],
    )
    servicer_context.abort_with_status(rpc_status.to_status(rich_status))


class _GenericHandler(grpc.GenericRpcHandler):
    def service(self, handler_call_details):
        if handler_call_details.method == _STATUS_OK:
            return grpc.unary_unary_rpc_method_handler(_ok_unary_unary)
        elif handler_call_details.method == _ERROR_DETAILS:
            return grpc.unary_unary_rpc_method_handler(
                _error_details_unary_unary
            )
        else:
            return None


class StatusConcurrencyTest(ConcurrencyTestCase):
    def setUp(self):
        super().setUp()
        # status uses a generic handler + raw channel (no generated stub)
        _, self._channel = self.start_server(
            None,
            lambda _servicer, server: server.add_generic_rpc_handlers(
                (_GenericHandler(),)
            ),
            lambda channel: channel,
        )

    def _status_ok(self, unused_index):
        for _ in range(ITERATIONS_PER_TASK):
            call = self._channel.unary_unary(_STATUS_OK)(_REQUEST)
            call
            # Succeed RPC doesn't have status
            status = rpc_status.from_call(call)
            self.assertIsNone(status)

    def _error_details(self, unused_index):
        for _ in range(ITERATIONS_PER_TASK):
            call = self._channel.unary_unary(_ERROR_DETAILS)(_REQUEST)
            with self.assertRaises(grpc.RpcError):
                call

            status = rpc_status.from_call(call)
            self.assertEqual(rpc_error.code(), grpc.StatusCode.INTERNAL)
            self.assertEqual(status.code, code_pb2.Code.Value("INTERNAL"))

            # Check if the underlying proto message is intact
            self.assertTrue(
                status.details[0].Is(error_details_pb2.DebugInfo.DESCRIPTOR)
            )
            info = error_details_pb2.DebugInfo()
            status.details[0].Unpack(info)
            self.assertIn("_error_details_unary_unary", info.stack_entries[-1])

    def test_concurrent_error_details(self):
        self.spawn_workers(self._error_details)

    def test_concurrent_ok_and_error(self):
        self.spawn_workers(self._status_ok, self._error_details)

if __name__ == "__main__":
    logging.basicConfig()
    unittest.main(verbosity=2)
