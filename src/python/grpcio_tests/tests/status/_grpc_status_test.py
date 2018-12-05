# Copyright 2018 The gRPC Authors
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
"""Tests of grpc_status."""

import unittest

import logging
import traceback

import grpc
from grpc_status import rpc_status

from tests.unit import test_common

from google.protobuf import any_pb2
from google.rpc import code_pb2, status_pb2, error_details_pb2

_STATUS_OK = '/test/StatusOK'
_STATUS_NOT_OK = '/test/StatusNotOk'
_ERROR_DETAILS = '/test/ErrorDetails'
_STATUS_REWRITE = '/test/StatusRewrite'
_STATUS_REWRITE_REVERSE = '/test/StatusRewriteReverse'
_INVALID_CODE = '/test/InvalidCode'

_REQUEST = b'\x00\x00\x00'
_RESPONSE = b'\x01\x01\x01'


def _ok_unary_unary(request, servicer_context):
    return _RESPONSE


def _not_ok_unary_unary(request, servicer_context):
    servicer_context.set_status(
        code=grpc.StatusCode.PERMISSION_DENIED, message='Intended failure')


def _error_details_unary_unary(request, servicer_context):
    details = any_pb2.Any()
    details.Pack(
        error_details_pb2.DebugInfo(
            stack_entries=traceback.format_stack(),
            detail='Intensionally invoked'))
    rich_status = status_pb2.Status(
        code=code_pb2.Code.Value('INTERNAL'),
        message='It shall be aborted',
        details=[details],
    )
    servicer_context.set_status(*rpc_status.convert(rich_status))


def _status_rewrite_unary_unary(request, servicer_context):
    servicer_context.set_code(grpc.StatusCode.NOT_FOUND)

    rich_status = status_pb2.Status(
        code=code_pb2.Code.Value('INTERNAL'),
        message='It shall be aborted',
    )
    servicer_context.set_status(*rpc_status.convert(rich_status))


def _status_rewrite_reverse_unary_unary(request, servicer_context):
    rich_status = status_pb2.Status(
        code=code_pb2.Code.Value('INTERNAL'),
        message='It shall be aborted',
    )
    servicer_context.set_status(*rpc_status.convert(rich_status))

    servicer_context.set_code(grpc.StatusCode.NOT_FOUND)
    servicer_context.set_details('Another message')


def _invalid_code_unary_unary(request, servicer_context):
    rich_status = status_pb2.Status(
        code=42,
        message='Invalid code',
    )
    servicer_context.set_status(*rpc_status.convert(rich_status))


class _GenericHandler(grpc.GenericRpcHandler):

    def service(self, handler_call_details):
        if handler_call_details.method == _STATUS_OK:
            return grpc.unary_unary_rpc_method_handler(_ok_unary_unary)
        elif handler_call_details.method == _STATUS_NOT_OK:
            return grpc.unary_unary_rpc_method_handler(_not_ok_unary_unary)
        elif handler_call_details.method == _ERROR_DETAILS:
            return grpc.unary_unary_rpc_method_handler(
                _error_details_unary_unary)
        elif handler_call_details.method == _STATUS_REWRITE:
            return grpc.unary_unary_rpc_method_handler(
                _status_rewrite_unary_unary)
        elif handler_call_details.method == _STATUS_REWRITE_REVERSE:
            return grpc.unary_unary_rpc_method_handler(
                _status_rewrite_reverse_unary_unary)
        elif handler_call_details.method == _INVALID_CODE:
            return grpc.unary_unary_rpc_method_handler(
                _invalid_code_unary_unary)
        else:
            return None


class StatusTest(unittest.TestCase):

    def setUp(self):
        self._server = test_common.test_server()
        self._server.add_generic_rpc_handlers((_GenericHandler(),))
        port = self._server.add_insecure_port('[::]:0')
        self._server.start()

        self._channel = grpc.insecure_channel('localhost:%d' % port)

    def tearDown(self):
        self._server.stop(None)
        self._channel.close()

    def test_status_ok(self):
        try:
            self._channel.unary_unary(_STATUS_OK)(_REQUEST)
        except grpc.RpcError as rpc_error:
            self.fail(rpc_error)

    def test_status_not_ok(self):
        with self.assertRaises(grpc.RpcError) as exception_context:
            self._channel.unary_unary(_STATUS_NOT_OK).with_call(_REQUEST)
        rpc_error = exception_context.exception

        self.assertEqual(rpc_error.status().code,
                         grpc.StatusCode.PERMISSION_DENIED)
        status = rpc_status.from_rpc_error(rpc_error)
        self.assertIs(status, None)

    def test_error_details(self):
        with self.assertRaises(grpc.RpcError) as exception_context:
            self._channel.unary_unary(_ERROR_DETAILS).with_call(_REQUEST)
        rpc_error = exception_context.exception

        status = rpc_status.from_rpc_error(rpc_error)
        self.assertEqual(rpc_error.status().code, grpc.StatusCode.INTERNAL)
        self.assertEqual(status.code, code_pb2.Code.Value('INTERNAL'))

        self.assertEqual(status.details[0].Is(
            error_details_pb2.DebugInfo.DESCRIPTOR), True)
        info = error_details_pb2.DebugInfo()
        status.details[0].Unpack(info)
        self.assertIn('_error_details_unary_unary', info.stack_entries[-1])

    def test_status_rewrite(self):
        """This tests a behavior (flaw) of current API design that code and
        message can be set multiple times. Since it is a longlived behavior,
        this unit test will serve as a regression test for it."""
        with self.assertRaises(grpc.RpcError) as exception_context:
            self._channel.unary_unary(_STATUS_REWRITE).with_call(_REQUEST)
        rpc_error = exception_context.exception

        self.assertEqual(rpc_error.status().code, grpc.StatusCode.INTERNAL)
        status = rpc_status.from_rpc_error(rpc_error)
        self.assertEqual(status.code, rpc_error.status().code.value[0])
        self.assertEqual(status.message, rpc_error.status().message)

    def test_code_message_validation(self):
        with self.assertRaises(grpc.RpcError) as exception_context:
            self._channel.unary_unary(_STATUS_REWRITE_REVERSE).with_call(
                _REQUEST)
        rpc_error = exception_context.exception
        self.assertEqual(rpc_error.status().code, grpc.StatusCode.NOT_FOUND)

        self.assertRaises(ValueError, rpc_status.from_rpc_error, rpc_error)

    def test_invalid_code(self):
        with self.assertRaises(grpc.RpcError) as exception_context:
            self._channel.unary_unary(_INVALID_CODE).with_call(_REQUEST)
        rpc_error = exception_context.exception
        self.assertEqual(rpc_error.status().code, grpc.StatusCode.UNKNOWN)
        self.assertIn('Invalid status code', rpc_error.status().message)


if __name__ == '__main__':
    logging.basicConfig()
    unittest.main(verbosity=2)
