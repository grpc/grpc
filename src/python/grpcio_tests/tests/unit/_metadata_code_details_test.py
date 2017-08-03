# Copyright 2016 gRPC authors.
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
"""Tests application-provided metadata, status code, and details."""

import threading
import unittest

import grpc
from grpc.framework.foundation import logging_pool

from tests.unit import test_common
from tests.unit.framework.common import test_constants
from tests.unit.framework.common import test_control

_SERIALIZED_REQUEST = b'\x46\x47\x48'
_SERIALIZED_RESPONSE = b'\x49\x50\x51'

_REQUEST_SERIALIZER = lambda unused_request: _SERIALIZED_REQUEST
_REQUEST_DESERIALIZER = lambda unused_serialized_request: object()
_RESPONSE_SERIALIZER = lambda unused_response: _SERIALIZED_RESPONSE
_RESPONSE_DESERIALIZER = lambda unused_serialized_resopnse: object()

_SERVICE = 'test.TestService'
_UNARY_UNARY = 'UnaryUnary'
_UNARY_STREAM = 'UnaryStream'
_STREAM_UNARY = 'StreamUnary'
_STREAM_STREAM = 'StreamStream'

_CLIENT_METADATA = (('client-md-key', 'client-md-key'),
                    ('client-md-key-bin', b'\x00\x01'))

_SERVER_INITIAL_METADATA = (
    ('server-initial-md-key', 'server-initial-md-value'),
    ('server-initial-md-key-bin', b'\x00\x02'))

_SERVER_TRAILING_METADATA = (
    ('server-trailing-md-key', 'server-trailing-md-value'),
    ('server-trailing-md-key-bin', b'\x00\x03'))

_NON_OK_CODE = grpc.StatusCode.NOT_FOUND
_DETAILS = 'Test details!'


class _Servicer(object):

    def __init__(self):
        self._lock = threading.Lock()
        self._code = None
        self._details = None
        self._exception = False
        self._return_none = False
        self._received_client_metadata = None

    def unary_unary(self, request, context):
        with self._lock:
            self._received_client_metadata = context.invocation_metadata()
            context.send_initial_metadata(_SERVER_INITIAL_METADATA)
            context.set_trailing_metadata(_SERVER_TRAILING_METADATA)
            if self._code is not None:
                context.set_code(self._code)
            if self._details is not None:
                context.set_details(self._details)
            if self._exception:
                raise test_control.Defect()
            else:
                return None if self._return_none else object()

    def unary_stream(self, request, context):
        with self._lock:
            self._received_client_metadata = context.invocation_metadata()
            context.send_initial_metadata(_SERVER_INITIAL_METADATA)
            context.set_trailing_metadata(_SERVER_TRAILING_METADATA)
            if self._code is not None:
                context.set_code(self._code)
            if self._details is not None:
                context.set_details(self._details)
            for _ in range(test_constants.STREAM_LENGTH // 2):
                yield _SERIALIZED_RESPONSE
            if self._exception:
                raise test_control.Defect()

    def stream_unary(self, request_iterator, context):
        with self._lock:
            self._received_client_metadata = context.invocation_metadata()
            context.send_initial_metadata(_SERVER_INITIAL_METADATA)
            context.set_trailing_metadata(_SERVER_TRAILING_METADATA)
            if self._code is not None:
                context.set_code(self._code)
            if self._details is not None:
                context.set_details(self._details)
            # TODO(https://github.com/grpc/grpc/issues/6891): just ignore the
            # request iterator.
            for ignored_request in request_iterator:
                pass
            if self._exception:
                raise test_control.Defect()
            else:
                return None if self._return_none else _SERIALIZED_RESPONSE

    def stream_stream(self, request_iterator, context):
        with self._lock:
            self._received_client_metadata = context.invocation_metadata()
            context.send_initial_metadata(_SERVER_INITIAL_METADATA)
            context.set_trailing_metadata(_SERVER_TRAILING_METADATA)
            if self._code is not None:
                context.set_code(self._code)
            if self._details is not None:
                context.set_details(self._details)
            # TODO(https://github.com/grpc/grpc/issues/6891): just ignore the
            # request iterator.
            for ignored_request in request_iterator:
                pass
            for _ in range(test_constants.STREAM_LENGTH // 3):
                yield object()
            if self._exception:
                raise test_control.Defect()

    def set_code(self, code):
        with self._lock:
            self._code = code

    def set_details(self, details):
        with self._lock:
            self._details = details

    def set_exception(self):
        with self._lock:
            self._exception = True

    def set_return_none(self):
        with self._lock:
            self._return_none = True

    def received_client_metadata(self):
        with self._lock:
            return self._received_client_metadata


def _generic_handler(servicer):
    method_handlers = {
        _UNARY_UNARY:
        grpc.unary_unary_rpc_method_handler(
            servicer.unary_unary,
            request_deserializer=_REQUEST_DESERIALIZER,
            response_serializer=_RESPONSE_SERIALIZER),
        _UNARY_STREAM:
        grpc.unary_stream_rpc_method_handler(servicer.unary_stream),
        _STREAM_UNARY:
        grpc.stream_unary_rpc_method_handler(servicer.stream_unary),
        _STREAM_STREAM:
        grpc.stream_stream_rpc_method_handler(
            servicer.stream_stream,
            request_deserializer=_REQUEST_DESERIALIZER,
            response_serializer=_RESPONSE_SERIALIZER),
    }
    return grpc.method_handlers_generic_handler(_SERVICE, method_handlers)


class MetadataCodeDetailsTest(unittest.TestCase):

    def setUp(self):
        self._servicer = _Servicer()
        self._server_pool = logging_pool.pool(test_constants.THREAD_CONCURRENCY)
        self._server = grpc.server(
            self._server_pool, handlers=(_generic_handler(self._servicer),))
        port = self._server.add_insecure_port('[::]:0')
        self._server.start()

        channel = grpc.insecure_channel('localhost:{}'.format(port))
        self._unary_unary = channel.unary_unary(
            '/'.join(('', _SERVICE, _UNARY_UNARY,)),
            request_serializer=_REQUEST_SERIALIZER,
            response_deserializer=_RESPONSE_DESERIALIZER,)
        self._unary_stream = channel.unary_stream(
            '/'.join(('', _SERVICE, _UNARY_STREAM,)),)
        self._stream_unary = channel.stream_unary(
            '/'.join(('', _SERVICE, _STREAM_UNARY,)),)
        self._stream_stream = channel.stream_stream(
            '/'.join(('', _SERVICE, _STREAM_STREAM,)),
            request_serializer=_REQUEST_SERIALIZER,
            response_deserializer=_RESPONSE_DESERIALIZER,)

    def testSuccessfulUnaryUnary(self):
        self._servicer.set_details(_DETAILS)

        unused_response, call = self._unary_unary.with_call(
            object(), metadata=_CLIENT_METADATA)

        self.assertTrue(
            test_common.metadata_transmitted(
                _CLIENT_METADATA, self._servicer.received_client_metadata()))
        self.assertTrue(
            test_common.metadata_transmitted(_SERVER_INITIAL_METADATA,
                                             call.initial_metadata()))
        self.assertTrue(
            test_common.metadata_transmitted(_SERVER_TRAILING_METADATA,
                                             call.trailing_metadata()))
        self.assertIs(grpc.StatusCode.OK, call.code())
        self.assertEqual(_DETAILS, call.details())

    def testSuccessfulUnaryStream(self):
        self._servicer.set_details(_DETAILS)

        call = self._unary_stream(
            _SERIALIZED_REQUEST, metadata=_CLIENT_METADATA)
        received_initial_metadata = call.initial_metadata()
        for _ in call:
            pass

        self.assertTrue(
            test_common.metadata_transmitted(
                _CLIENT_METADATA, self._servicer.received_client_metadata()))
        self.assertTrue(
            test_common.metadata_transmitted(_SERVER_INITIAL_METADATA,
                                             received_initial_metadata))
        self.assertTrue(
            test_common.metadata_transmitted(_SERVER_TRAILING_METADATA,
                                             call.trailing_metadata()))
        self.assertIs(grpc.StatusCode.OK, call.code())
        self.assertEqual(_DETAILS, call.details())

    def testSuccessfulStreamUnary(self):
        self._servicer.set_details(_DETAILS)

        unused_response, call = self._stream_unary.with_call(
            iter([_SERIALIZED_REQUEST] * test_constants.STREAM_LENGTH),
            metadata=_CLIENT_METADATA)

        self.assertTrue(
            test_common.metadata_transmitted(
                _CLIENT_METADATA, self._servicer.received_client_metadata()))
        self.assertTrue(
            test_common.metadata_transmitted(_SERVER_INITIAL_METADATA,
                                             call.initial_metadata()))
        self.assertTrue(
            test_common.metadata_transmitted(_SERVER_TRAILING_METADATA,
                                             call.trailing_metadata()))
        self.assertIs(grpc.StatusCode.OK, call.code())
        self.assertEqual(_DETAILS, call.details())

    def testSuccessfulStreamStream(self):
        self._servicer.set_details(_DETAILS)

        call = self._stream_stream(
            iter([object()] * test_constants.STREAM_LENGTH),
            metadata=_CLIENT_METADATA)
        received_initial_metadata = call.initial_metadata()
        for _ in call:
            pass

        self.assertTrue(
            test_common.metadata_transmitted(
                _CLIENT_METADATA, self._servicer.received_client_metadata()))
        self.assertTrue(
            test_common.metadata_transmitted(_SERVER_INITIAL_METADATA,
                                             received_initial_metadata))
        self.assertTrue(
            test_common.metadata_transmitted(_SERVER_TRAILING_METADATA,
                                             call.trailing_metadata()))
        self.assertIs(grpc.StatusCode.OK, call.code())
        self.assertEqual(_DETAILS, call.details())

    def testCustomCodeUnaryUnary(self):
        self._servicer.set_code(_NON_OK_CODE)
        self._servicer.set_details(_DETAILS)

        with self.assertRaises(grpc.RpcError) as exception_context:
            self._unary_unary.with_call(object(), metadata=_CLIENT_METADATA)

        self.assertTrue(
            test_common.metadata_transmitted(
                _CLIENT_METADATA, self._servicer.received_client_metadata()))
        self.assertTrue(
            test_common.metadata_transmitted(
                _SERVER_INITIAL_METADATA,
                exception_context.exception.initial_metadata()))
        self.assertTrue(
            test_common.metadata_transmitted(
                _SERVER_TRAILING_METADATA,
                exception_context.exception.trailing_metadata()))
        self.assertIs(_NON_OK_CODE, exception_context.exception.code())
        self.assertEqual(_DETAILS, exception_context.exception.details())

    def testCustomCodeUnaryStream(self):
        self._servicer.set_code(_NON_OK_CODE)
        self._servicer.set_details(_DETAILS)

        call = self._unary_stream(
            _SERIALIZED_REQUEST, metadata=_CLIENT_METADATA)
        received_initial_metadata = call.initial_metadata()
        with self.assertRaises(grpc.RpcError):
            for _ in call:
                pass

        self.assertTrue(
            test_common.metadata_transmitted(
                _CLIENT_METADATA, self._servicer.received_client_metadata()))
        self.assertTrue(
            test_common.metadata_transmitted(_SERVER_INITIAL_METADATA,
                                             received_initial_metadata))
        self.assertTrue(
            test_common.metadata_transmitted(_SERVER_TRAILING_METADATA,
                                             call.trailing_metadata()))
        self.assertIs(_NON_OK_CODE, call.code())
        self.assertEqual(_DETAILS, call.details())

    def testCustomCodeStreamUnary(self):
        self._servicer.set_code(_NON_OK_CODE)
        self._servicer.set_details(_DETAILS)

        with self.assertRaises(grpc.RpcError) as exception_context:
            self._stream_unary.with_call(
                iter([_SERIALIZED_REQUEST] * test_constants.STREAM_LENGTH),
                metadata=_CLIENT_METADATA)

        self.assertTrue(
            test_common.metadata_transmitted(
                _CLIENT_METADATA, self._servicer.received_client_metadata()))
        self.assertTrue(
            test_common.metadata_transmitted(
                _SERVER_INITIAL_METADATA,
                exception_context.exception.initial_metadata()))
        self.assertTrue(
            test_common.metadata_transmitted(
                _SERVER_TRAILING_METADATA,
                exception_context.exception.trailing_metadata()))
        self.assertIs(_NON_OK_CODE, exception_context.exception.code())
        self.assertEqual(_DETAILS, exception_context.exception.details())

    def testCustomCodeStreamStream(self):
        self._servicer.set_code(_NON_OK_CODE)
        self._servicer.set_details(_DETAILS)

        call = self._stream_stream(
            iter([object()] * test_constants.STREAM_LENGTH),
            metadata=_CLIENT_METADATA)
        received_initial_metadata = call.initial_metadata()
        with self.assertRaises(grpc.RpcError) as exception_context:
            for _ in call:
                pass

        self.assertTrue(
            test_common.metadata_transmitted(
                _CLIENT_METADATA, self._servicer.received_client_metadata()))
        self.assertTrue(
            test_common.metadata_transmitted(_SERVER_INITIAL_METADATA,
                                             received_initial_metadata))
        self.assertTrue(
            test_common.metadata_transmitted(
                _SERVER_TRAILING_METADATA,
                exception_context.exception.trailing_metadata()))
        self.assertIs(_NON_OK_CODE, exception_context.exception.code())
        self.assertEqual(_DETAILS, exception_context.exception.details())

    def testCustomCodeExceptionUnaryUnary(self):
        self._servicer.set_code(_NON_OK_CODE)
        self._servicer.set_details(_DETAILS)
        self._servicer.set_exception()

        with self.assertRaises(grpc.RpcError) as exception_context:
            self._unary_unary.with_call(object(), metadata=_CLIENT_METADATA)

        self.assertTrue(
            test_common.metadata_transmitted(
                _CLIENT_METADATA, self._servicer.received_client_metadata()))
        self.assertTrue(
            test_common.metadata_transmitted(
                _SERVER_INITIAL_METADATA,
                exception_context.exception.initial_metadata()))
        self.assertTrue(
            test_common.metadata_transmitted(
                _SERVER_TRAILING_METADATA,
                exception_context.exception.trailing_metadata()))
        self.assertIs(_NON_OK_CODE, exception_context.exception.code())
        self.assertEqual(_DETAILS, exception_context.exception.details())

    def testCustomCodeExceptionUnaryStream(self):
        self._servicer.set_code(_NON_OK_CODE)
        self._servicer.set_details(_DETAILS)
        self._servicer.set_exception()

        call = self._unary_stream(
            _SERIALIZED_REQUEST, metadata=_CLIENT_METADATA)
        received_initial_metadata = call.initial_metadata()
        with self.assertRaises(grpc.RpcError):
            for _ in call:
                pass

        self.assertTrue(
            test_common.metadata_transmitted(
                _CLIENT_METADATA, self._servicer.received_client_metadata()))
        self.assertTrue(
            test_common.metadata_transmitted(_SERVER_INITIAL_METADATA,
                                             received_initial_metadata))
        self.assertTrue(
            test_common.metadata_transmitted(_SERVER_TRAILING_METADATA,
                                             call.trailing_metadata()))
        self.assertIs(_NON_OK_CODE, call.code())
        self.assertEqual(_DETAILS, call.details())

    def testCustomCodeExceptionStreamUnary(self):
        self._servicer.set_code(_NON_OK_CODE)
        self._servicer.set_details(_DETAILS)
        self._servicer.set_exception()

        with self.assertRaises(grpc.RpcError) as exception_context:
            self._stream_unary.with_call(
                iter([_SERIALIZED_REQUEST] * test_constants.STREAM_LENGTH),
                metadata=_CLIENT_METADATA)

        self.assertTrue(
            test_common.metadata_transmitted(
                _CLIENT_METADATA, self._servicer.received_client_metadata()))
        self.assertTrue(
            test_common.metadata_transmitted(
                _SERVER_INITIAL_METADATA,
                exception_context.exception.initial_metadata()))
        self.assertTrue(
            test_common.metadata_transmitted(
                _SERVER_TRAILING_METADATA,
                exception_context.exception.trailing_metadata()))
        self.assertIs(_NON_OK_CODE, exception_context.exception.code())
        self.assertEqual(_DETAILS, exception_context.exception.details())

    def testCustomCodeExceptionStreamStream(self):
        self._servicer.set_code(_NON_OK_CODE)
        self._servicer.set_details(_DETAILS)
        self._servicer.set_exception()

        call = self._stream_stream(
            iter([object()] * test_constants.STREAM_LENGTH),
            metadata=_CLIENT_METADATA)
        received_initial_metadata = call.initial_metadata()
        with self.assertRaises(grpc.RpcError):
            for _ in call:
                pass

        self.assertTrue(
            test_common.metadata_transmitted(
                _CLIENT_METADATA, self._servicer.received_client_metadata()))
        self.assertTrue(
            test_common.metadata_transmitted(_SERVER_INITIAL_METADATA,
                                             received_initial_metadata))
        self.assertTrue(
            test_common.metadata_transmitted(_SERVER_TRAILING_METADATA,
                                             call.trailing_metadata()))
        self.assertIs(_NON_OK_CODE, call.code())
        self.assertEqual(_DETAILS, call.details())

    def testCustomCodeReturnNoneUnaryUnary(self):
        self._servicer.set_code(_NON_OK_CODE)
        self._servicer.set_details(_DETAILS)
        self._servicer.set_return_none()

        with self.assertRaises(grpc.RpcError) as exception_context:
            self._unary_unary.with_call(object(), metadata=_CLIENT_METADATA)

        self.assertTrue(
            test_common.metadata_transmitted(
                _CLIENT_METADATA, self._servicer.received_client_metadata()))
        self.assertTrue(
            test_common.metadata_transmitted(
                _SERVER_INITIAL_METADATA,
                exception_context.exception.initial_metadata()))
        self.assertTrue(
            test_common.metadata_transmitted(
                _SERVER_TRAILING_METADATA,
                exception_context.exception.trailing_metadata()))
        self.assertIs(_NON_OK_CODE, exception_context.exception.code())
        self.assertEqual(_DETAILS, exception_context.exception.details())

    def testCustomCodeReturnNoneStreamUnary(self):
        self._servicer.set_code(_NON_OK_CODE)
        self._servicer.set_details(_DETAILS)
        self._servicer.set_return_none()

        with self.assertRaises(grpc.RpcError) as exception_context:
            self._stream_unary.with_call(
                iter([_SERIALIZED_REQUEST] * test_constants.STREAM_LENGTH),
                metadata=_CLIENT_METADATA)

        self.assertTrue(
            test_common.metadata_transmitted(
                _CLIENT_METADATA, self._servicer.received_client_metadata()))
        self.assertTrue(
            test_common.metadata_transmitted(
                _SERVER_INITIAL_METADATA,
                exception_context.exception.initial_metadata()))
        self.assertTrue(
            test_common.metadata_transmitted(
                _SERVER_TRAILING_METADATA,
                exception_context.exception.trailing_metadata()))
        self.assertIs(_NON_OK_CODE, exception_context.exception.code())
        self.assertEqual(_DETAILS, exception_context.exception.details())


if __name__ == '__main__':
    unittest.main(verbosity=2)
