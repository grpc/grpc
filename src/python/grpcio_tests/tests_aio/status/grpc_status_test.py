# Copyright 2020 The gRPC Authors
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
"""Tests of grpc_status with gRPC AsyncIO stack."""

import logging
import traceback
import unittest

from google.protobuf import any_pb2
from google.rpc import code_pb2
from google.rpc import error_details_pb2
from google.rpc import status_pb2
import grpc
from grpc.experimental import aio
from grpc_status import rpc_status

from tests_aio.unit._test_base import AioTestBase

_STATUS_OK = "/test/StatusOK"
_STATUS_NOT_OK = "/test/StatusNotOk"
_ERROR_DETAILS = "/test/ErrorDetails"
_INCONSISTENT = "/test/Inconsistent"
_INVALID_CODE = "/test/InvalidCode"

_REQUEST = b"\x00\x00\x00"
_RESPONSE = b"\x01\x01\x01"

_GRPC_DETAILS_METADATA_KEY = "grpc-status-details-bin"

_STATUS_DETAILS = "This is an error detail"
_STATUS_DETAILS_ANOTHER = "This is another error detail"


async def _ok_unary_unary(request, servicer_context):
    return _RESPONSE


async def _not_ok_unary_unary(request, servicer_context):
    await servicer_context.abort(grpc.StatusCode.INTERNAL, _STATUS_DETAILS)


async def _error_details_unary_unary(request, servicer_context):
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
    await servicer_context.abort_with_status(rpc_status.to_status(rich_status))


async def _inconsistent_unary_unary(request, servicer_context):
    rich_status = status_pb2.Status(
        code=code_pb2.INTERNAL,
        message=_STATUS_DETAILS,
    )
    servicer_context.set_code(grpc.StatusCode.NOT_FOUND)
    servicer_context.set_details(_STATUS_DETAILS_ANOTHER)
    # User put inconsistent status information in trailing metadata
    servicer_context.set_trailing_metadata(
        ((_GRPC_DETAILS_METADATA_KEY, rich_status.SerializeToString()),)
    )


async def _invalid_code_unary_unary(request, servicer_context):
    rich_status = status_pb2.Status(
        code=42,
        message="Invalid code",
    )
    await servicer_context.abort_with_status(rpc_status.to_status(rich_status))


class _GenericHandler(grpc.GenericRpcHandler):
    def service(self, handler_call_details):
        if handler_call_details.method == _STATUS_OK:
            return grpc.unary_unary_rpc_method_handler(_ok_unary_unary)
        elif handler_call_details.method == _STATUS_NOT_OK:
            return grpc.unary_unary_rpc_method_handler(_not_ok_unary_unary)
        elif handler_call_details.method == _ERROR_DETAILS:
            return grpc.unary_unary_rpc_method_handler(
                _error_details_unary_unary
            )
        elif handler_call_details.method == _INCONSISTENT:
            return grpc.unary_unary_rpc_method_handler(
                _inconsistent_unary_unary
            )
        elif handler_call_details.method == _INVALID_CODE:
            return grpc.unary_unary_rpc_method_handler(
                _invalid_code_unary_unary
            )
        else:
            return None


class StatusTest(AioTestBase):
    async def setUp(self):
        self._server = aio.server()
        self._server.add_generic_rpc_handlers((_GenericHandler(),))
        port = self._server.add_insecure_port("[::]:0")
        await self._server.start()

        self._channel = aio.insecure_channel("localhost:%d" % port)

    async def tearDown(self):
        await self._server.stop(None)
        await self._channel.close()

    async def test_status_ok(self):
        call = self._channel.unary_unary(_STATUS_OK)(_REQUEST)

        # Succeed RPC doesn't have status
        status = await rpc_status.aio.from_call(call)
        self.assertIs(status, None)

    async def test_status_not_ok(self):
        call = self._channel.unary_unary(_STATUS_NOT_OK)(_REQUEST)
        with self.assertRaises(aio.AioRpcError) as exception_context:
            await call
        rpc_error = exception_context.exception

        self.assertEqual(rpc_error.code(), grpc.StatusCode.INTERNAL)
        # Failed RPC doesn't automatically generate status
        status = await rpc_status.aio.from_call(call)
        self.assertIs(status, None)

    async def test_error_details(self):
        call = self._channel.unary_unary(_ERROR_DETAILS)(_REQUEST)
        with self.assertRaises(aio.AioRpcError) as exception_context:
            await call
        rpc_error = exception_context.exception

        status = await rpc_status.aio.from_call(call)
        self.assertEqual(rpc_error.code(), grpc.StatusCode.INTERNAL)
        self.assertEqual(status.code, code_pb2.Code.Value("INTERNAL"))

        # Check if the underlying proto message is intact
        self.assertTrue(
            status.details[0].Is(error_details_pb2.DebugInfo.DESCRIPTOR)
        )
        info = error_details_pb2.DebugInfo()
        status.details[0].Unpack(info)
        self.assertIn("_error_details_unary_unary", info.stack_entries[-1])

    async def test_code_message_validation(self):
        call = self._channel.unary_unary(_INCONSISTENT)(_REQUEST)
        with self.assertRaises(aio.AioRpcError) as exception_context:
            await call
        rpc_error = exception_context.exception
        self.assertEqual(rpc_error.code(), grpc.StatusCode.NOT_FOUND)

        # Code/Message validation failed
        with self.assertRaises(ValueError):
            await rpc_status.aio.from_call(call)

    async def test_invalid_code(self):
        with self.assertRaises(aio.AioRpcError) as exception_context:
            await self._channel.unary_unary(_INVALID_CODE)(_REQUEST)
        rpc_error = exception_context.exception
        self.assertEqual(rpc_error.code(), grpc.StatusCode.UNKNOWN)
        # Invalid status code exception raised during coversion
        self.assertIn("Invalid status code", rpc_error.details())


if __name__ == "__main__":
    logging.basicConfig()
    unittest.main(verbosity=2)
