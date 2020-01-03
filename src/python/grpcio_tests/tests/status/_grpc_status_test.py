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

# NOTE(lidiz) This module only exists in Bazel BUILD file, for more details
# please refer to comments in the "bazel_namespace_package_hack" module.
try:
    from tests import bazel_namespace_package_hack
    bazel_namespace_package_hack.sys_path_to_site_dir_hack()
except ImportError:
    pass

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
_INCONSISTENT = '/test/Inconsistent'
_INVALID_CODE = '/test/InvalidCode'

_REQUEST = b'\x00\x00\x00'
_RESPONSE = b'\x01\x01\x01'

_GRPC_DETAILS_METADATA_KEY = 'grpc-status-details-bin'

_STATUS_DETAILS = 'This is an error detail'
_STATUS_DETAILS_ANOTHER = 'This is another error detail'


def _ok_unary_unary(request, servicer_context):
    return _RESPONSE


def _not_ok_unary_unary(request, servicer_context):
    servicer_context.abort(grpc.StatusCode.INTERNAL, _STATUS_DETAILS)


def _error_details_unary_unary(request, servicer_context):
    details = any_pb2.Any()
    details.Pack(
        error_details_pb2.DebugInfo(stack_entries=traceback.format_stack(),
                                    detail='Intentionally invoked'))
    rich_status = status_pb2.Status(
        code=code_pb2.INTERNAL,
        message=_STATUS_DETAILS,
        details=[details],
    )
    servicer_context.abort_with_status(rpc_status.to_status(rich_status))


def _inconsistent_unary_unary(request, servicer_context):
    rich_status = status_pb2.Status(
        code=code_pb2.INTERNAL,
        message=_STATUS_DETAILS,
    )
    servicer_context.set_code(grpc.StatusCode.NOT_FOUND)
    servicer_context.set_details(_STATUS_DETAILS_ANOTHER)
    # User put inconsistent status information in trailing metadata
    servicer_context.set_trailing_metadata(
        ((_GRPC_DETAILS_METADATA_KEY, rich_status.SerializeToString()),))


def _invalid_code_unary_unary(request, servicer_context):
    rich_status = status_pb2.Status(
        code=42,
        message='Invalid code',
    )
    servicer_context.abort_with_status(rpc_status.to_status(rich_status))


class _GenericHandler(grpc.GenericRpcHandler):

    def service(self, handler_call_details):
        if handler_call_details.method == _STATUS_OK:
            return grpc.unary_unary_rpc_method_handler(_ok_unary_unary)
        elif handler_call_details.method == _STATUS_NOT_OK:
            return grpc.unary_unary_rpc_method_handler(_not_ok_unary_unary)
        elif handler_call_details.method == _ERROR_DETAILS:
            return grpc.unary_unary_rpc_method_handler(
                _error_details_unary_unary)
        elif handler_call_details.method == _INCONSISTENT:
            return grpc.unary_unary_rpc_method_handler(
                _inconsistent_unary_unary)
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
        _, call = self._channel.unary_unary(_STATUS_OK).with_call(_REQUEST)

        # Succeed RPC doesn't have status
        status = rpc_status.from_call(call)
        self.assertIs(status, None)

    def test_status_not_ok(self):
        with self.assertRaises(grpc.RpcError) as exception_context:
            self._channel.unary_unary(_STATUS_NOT_OK).with_call(_REQUEST)
        rpc_error = exception_context.exception

        self.assertEqual(rpc_error.code(), grpc.StatusCode.INTERNAL)
        # Failed RPC doesn't automatically generate status
        status = rpc_status.from_call(rpc_error)
        self.assertIs(status, None)

    def test_error_details(self):
        with self.assertRaises(grpc.RpcError) as exception_context:
            self._channel.unary_unary(_ERROR_DETAILS).with_call(_REQUEST)
        rpc_error = exception_context.exception

        status = rpc_status.from_call(rpc_error)
        self.assertEqual(rpc_error.code(), grpc.StatusCode.INTERNAL)
        self.assertEqual(status.code, code_pb2.Code.Value('INTERNAL'))

        # Check if the underlying proto message is intact
        self.assertEqual(
            status.details[0].Is(error_details_pb2.DebugInfo.DESCRIPTOR), True)
        info = error_details_pb2.DebugInfo()
        status.details[0].Unpack(info)
        self.assertIn('_error_details_unary_unary', info.stack_entries[-1])

    def test_code_message_validation(self):
        with self.assertRaises(grpc.RpcError) as exception_context:
            self._channel.unary_unary(_INCONSISTENT).with_call(_REQUEST)
        rpc_error = exception_context.exception
        self.assertEqual(rpc_error.code(), grpc.StatusCode.NOT_FOUND)

        # Code/Message validation failed
        self.assertRaises(ValueError, rpc_status.from_call, rpc_error)

    def test_invalid_code(self):
        with self.assertRaises(grpc.RpcError) as exception_context:
            self._channel.unary_unary(_INVALID_CODE).with_call(_REQUEST)
        rpc_error = exception_context.exception
        self.assertEqual(rpc_error.code(), grpc.StatusCode.UNKNOWN)
        # Invalid status code exception raised during coversion
        self.assertIn('Invalid status code', rpc_error.details())


if __name__ == '__main__':
    logging.basicConfig()
    unittest.main(verbosity=2)
