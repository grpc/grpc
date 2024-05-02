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

import logging
import unittest

import grpc

from tests.unit import test_common
from tests.unit.framework.common import test_constants
from tests.unit.framework.common import test_control

_SERIALIZE_REQUEST = lambda bytestring: bytestring * 2
_DESERIALIZE_REQUEST = lambda bytestring: bytestring[len(bytestring) // 2 :]
_SERIALIZE_RESPONSE = lambda bytestring: bytestring * 3
_DESERIALIZE_RESPONSE = lambda bytestring: bytestring[: len(bytestring) // 3]

_SERVICE_NAME = "test"
_UNARY_UNARY = "UnaryUnary"
_UNARY_UNARY_NESTED_EXCEPTION = "UnaryUnaryNestedException"
_UNARY_STREAM = "UnaryStream"
_STREAM_UNARY = "StreamUnary"
_STREAM_STREAM = "StreamStream"
_DEFECTIVE_GENERIC_RPC_HANDLER = "DefectiveGenericRpcHandler"


class _Handler(object):
    def __init__(self, control):
        self._control = control

    def handle_unary_unary(self, request, servicer_context):
        self._control.control()
        if servicer_context is not None:
            servicer_context.set_trailing_metadata(
                (
                    (
                        "testkey",
                        "testvalue",
                    ),
                )
            )
        return request

    def handle_unary_unary_with_nested_exception(
        self, request, servicer_context
    ):
        raise test_control.NestedDefect()

    def handle_unary_stream(self, request, servicer_context):
        for _ in range(test_constants.STREAM_LENGTH):
            self._control.control()
            yield request
        self._control.control()
        if servicer_context is not None:
            servicer_context.set_trailing_metadata(
                (
                    (
                        "testkey",
                        "testvalue",
                    ),
                )
            )

    def handle_stream_unary(self, request_iterator, servicer_context):
        if servicer_context is not None:
            servicer_context.invocation_metadata()
        self._control.control()
        response_elements = []
        for request in request_iterator:
            self._control.control()
            response_elements.append(request)
        self._control.control()
        if servicer_context is not None:
            servicer_context.set_trailing_metadata(
                (
                    (
                        "testkey",
                        "testvalue",
                    ),
                )
            )
        return b"".join(response_elements)

    def handle_stream_stream(self, request_iterator, servicer_context):
        self._control.control()
        if servicer_context is not None:
            servicer_context.set_trailing_metadata(
                (
                    (
                        "testkey",
                        "testvalue",
                    ),
                )
            )
        for request in request_iterator:
            self._control.control()
            yield request
        self._control.control()

    def defective_generic_rpc_handler(self):
        raise test_control.Defect()


class _MethodHandler(grpc.RpcMethodHandler):
    def __init__(
        self,
        request_streaming,
        response_streaming,
        request_deserializer,
        response_serializer,
        unary_unary,
        unary_stream,
        stream_unary,
        stream_stream,
    ):
        self.request_streaming = request_streaming
        self.response_streaming = response_streaming
        self.request_deserializer = request_deserializer
        self.response_serializer = response_serializer
        self.unary_unary = unary_unary
        self.unary_stream = unary_stream
        self.stream_unary = stream_unary
        self.stream_stream = stream_stream


def get_method_handlers(handler):
    return {
        _UNARY_UNARY: _MethodHandler(
            False,
            False,
            None,
            None,
            handler.handle_unary_unary,
            None,
            None,
            None,
        ),
        _UNARY_STREAM: _MethodHandler(
            False,
            True,
            _DESERIALIZE_REQUEST,
            _SERIALIZE_RESPONSE,
            None,
            handler.handle_unary_stream,
            None,
            None,
        ),
        _STREAM_UNARY: _MethodHandler(
            True,
            False,
            _DESERIALIZE_REQUEST,
            _SERIALIZE_RESPONSE,
            None,
            None,
            handler.handle_stream_unary,
            None,
        ),
        _STREAM_STREAM: _MethodHandler(
            True,
            True,
            None,
            None,
            None,
            None,
            None,
            handler.handle_stream_stream,
        ),
        _DEFECTIVE_GENERIC_RPC_HANDLER: _MethodHandler(
            False,
            False,
            None,
            None,
            handler.defective_generic_rpc_handler,
            None,
            None,
            None,
        ),
        _UNARY_UNARY_NESTED_EXCEPTION: _MethodHandler(
            False,
            False,
            None,
            None,
            handler.handle_unary_unary_with_nested_exception,
            None,
            None,
            None,
        ),
    }


class FailAfterFewIterationsCounter(object):
    def __init__(self, high, bytestring):
        self._current = 0
        self._high = high
        self._bytestring = bytestring

    def __iter__(self):
        return self

    def __next__(self):
        if self._current >= self._high:
            raise test_control.Defect()
        else:
            self._current += 1
            return self._bytestring

    next = __next__


def _unary_unary_multi_callable(channel):
    return channel.unary_unary(
        grpc._common.fully_qualified_method(_SERVICE_NAME, _UNARY_UNARY),
        _registered_method=True,
    )


def _unary_stream_multi_callable(channel):
    return channel.unary_stream(
        grpc._common.fully_qualified_method(_SERVICE_NAME, _UNARY_STREAM),
        request_serializer=_SERIALIZE_REQUEST,
        response_deserializer=_DESERIALIZE_RESPONSE,
        _registered_method=True,
    )


def _stream_unary_multi_callable(channel):
    return channel.stream_unary(
        grpc._common.fully_qualified_method(_SERVICE_NAME, _STREAM_UNARY),
        request_serializer=_SERIALIZE_REQUEST,
        response_deserializer=_DESERIALIZE_RESPONSE,
        _registered_method=True,
    )


def _stream_stream_multi_callable(channel):
    return channel.stream_stream(
        grpc._common.fully_qualified_method(_SERVICE_NAME, _STREAM_STREAM),
        _registered_method=True,
    )


def _defective_handler_multi_callable(channel):
    return channel.unary_unary(
        grpc._common.fully_qualified_method(
            _SERVICE_NAME, _DEFECTIVE_GENERIC_RPC_HANDLER
        ),
        _registered_method=True,
    )


def _defective_nested_exception_handler_multi_callable(channel):
    return channel.unary_unary(
        grpc._common.fully_qualified_method(
            _SERVICE_NAME, _UNARY_UNARY_NESTED_EXCEPTION
        ),
        _registered_method=True,
    )


class InvocationDefectsTest(unittest.TestCase):
    """Tests the handling of exception-raising user code on the client-side."""

    def setUp(self):
        self._control = test_control.PauseFailControl()
        self._handler = _Handler(self._control)

        self._server = test_common.test_server()
        port = self._server.add_insecure_port("[::]:0")
        self._server.add_registered_method_handlers(
            _SERVICE_NAME, get_method_handlers(self._handler)
        )
        self._server.start()

        self._channel = grpc.insecure_channel("localhost:%d" % port)

    def tearDown(self):
        self._server.stop(0)
        self._channel.close()

    def testIterableStreamRequestBlockingUnaryResponse(self):
        requests = object()
        multi_callable = _stream_unary_multi_callable(self._channel)

        with self.assertRaises(grpc.RpcError) as exception_context:
            multi_callable(
                requests,
                metadata=(
                    ("test", "IterableStreamRequestBlockingUnaryResponse"),
                ),
            )

        self.assertIs(
            grpc.StatusCode.UNKNOWN, exception_context.exception.code()
        )

    def testIterableStreamRequestFutureUnaryResponse(self):
        requests = object()
        multi_callable = _stream_unary_multi_callable(self._channel)
        response_future = multi_callable.future(
            requests,
            metadata=(("test", "IterableStreamRequestFutureUnaryResponse"),),
        )

        with self.assertRaises(grpc.RpcError) as exception_context:
            response_future.result()

        self.assertIs(
            grpc.StatusCode.UNKNOWN, exception_context.exception.code()
        )

    def testIterableStreamRequestStreamResponse(self):
        requests = object()
        multi_callable = _stream_stream_multi_callable(self._channel)
        response_iterator = multi_callable(
            requests,
            metadata=(("test", "IterableStreamRequestStreamResponse"),),
        )

        with self.assertRaises(grpc.RpcError) as exception_context:
            next(response_iterator)

        self.assertIs(
            grpc.StatusCode.UNKNOWN, exception_context.exception.code()
        )

    def testIteratorStreamRequestStreamResponse(self):
        requests_iterator = FailAfterFewIterationsCounter(
            test_constants.STREAM_LENGTH // 2, b"\x07\x08"
        )
        multi_callable = _stream_stream_multi_callable(self._channel)
        response_iterator = multi_callable(
            requests_iterator,
            metadata=(("test", "IteratorStreamRequestStreamResponse"),),
        )

        with self.assertRaises(grpc.RpcError) as exception_context:
            for _ in range(test_constants.STREAM_LENGTH // 2 + 1):
                next(response_iterator)

        self.assertIs(
            grpc.StatusCode.UNKNOWN, exception_context.exception.code()
        )

    def testDefectiveGenericRpcHandlerUnaryResponse(self):
        request = b"\x07\x08"
        multi_callable = _defective_handler_multi_callable(self._channel)

        with self.assertRaises(grpc.RpcError) as exception_context:
            multi_callable(
                request, metadata=(("test", "DefectiveGenericRpcHandlerUnary"),)
            )

        self.assertIs(
            grpc.StatusCode.UNKNOWN, exception_context.exception.code()
        )

    def testNestedExceptionGenericRpcHandlerUnaryResponse(self):
        request = b"\x07\x08"
        multi_callable = _defective_nested_exception_handler_multi_callable(
            self._channel
        )

        with self.assertRaises(grpc.RpcError) as exception_context:
            multi_callable(
                request, metadata=(("test", "DefectiveGenericRpcHandlerUnary"),)
            )

        self.assertIs(
            grpc.StatusCode.UNKNOWN, exception_context.exception.code()
        )


if __name__ == "__main__":
    logging.basicConfig()
    unittest.main(verbosity=2)
