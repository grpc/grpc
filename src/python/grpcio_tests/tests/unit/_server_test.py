# Copyright 2018 The gRPC Authors.
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

from concurrent import futures
import logging
import unittest

import grpc

from tests.unit import resources
from tests.unit import test_common
from tests.unit.framework.common import test_constants

_REQUEST = b""
_RESPONSE = b"response"
_REGISTERED_RESPONSE = b"registered_response"

_SERVICE_NAME = "test"
_UNARY_UNARY = "UnaryUnary"
_UNARY_UNARY_REGISTERED = "UnaryUnaryRegistered"
_UNARY_STREAM = "UnaryStream"
_STREAM_UNARY = "StreamUnary"
_STREAM_STREAM = "StreamStream"


class _ActualGenericRpcHandler(grpc.GenericRpcHandler):
    def service(self, handler_call_details):
        return None


def handle_unary_unary(request, servicer_context):
    return _RESPONSE


def handle_registered_unary_unary(request, servicer_context):
    return _REGISTERED_RESPONSE


def handle_unary_stream(request, servicer_context):
    for _ in range(test_constants.STREAM_LENGTH):
        yield _RESPONSE


def handle_stream_unary(request_iterator, servicer_context):
    for request in request_iterator:
        pass
    return _RESPONSE


def handle_stream_stream(request_iterator, servicer_context):
    for request in request_iterator:
        yield _RESPONSE


class _MethodHandler(grpc.RpcMethodHandler):
    def __init__(self, request_streaming, response_streaming, registered=False):
        self.request_streaming = request_streaming
        self.response_streaming = response_streaming
        self.request_deserializer = None
        self.response_serializer = None
        self.unary_unary = None
        self.unary_stream = None
        self.stream_unary = None
        self.stream_stream = None
        if self.request_streaming and self.response_streaming:
            self.stream_stream = handle_stream_stream
        elif self.request_streaming:
            self.stream_unary = handle_stream_unary
        elif self.response_streaming:
            self.unary_stream = handle_unary_stream
        else:
            if registered:
                self.unary_unary = handle_registered_unary_unary
            else:
                self.unary_unary = handle_unary_unary


class _GenericHandler(grpc.GenericRpcHandler):
    def service(self, handler_call_details):
        if handler_call_details.method == _UNARY_UNARY:
            return _MethodHandler(False, False)
        elif handler_call_details.method == _UNARY_STREAM:
            return _MethodHandler(False, True)
        elif handler_call_details.method == _STREAM_UNARY:
            return _MethodHandler(True, False)
        elif handler_call_details.method == _STREAM_STREAM:
            return _MethodHandler(True, True)
        elif handler_call_details.method == grpc._common.fully_qualified_method(
            _SERVICE_NAME, _UNARY_UNARY_REGISTERED
        ):
            return _MethodHandler(False, False)
        else:
            return None


class _GenericHandlerWithRegisteredName(grpc.GenericRpcHandler):
    def service(self, handler_call_details):
        if handler_call_details.method == grpc._common.fully_qualified_method(
            _SERVICE_NAME, _UNARY_UNARY_REGISTERED
        ):
            return _MethodHandler(False, False)
        else:
            return None


_REGISTERED_METHOD_HANDLERS = {
    _UNARY_UNARY_REGISTERED: _MethodHandler(False, False, True),
}


class ServerTest(unittest.TestCase):
    def test_not_a_generic_rpc_handler_at_construction(self):
        with self.assertRaises(AttributeError) as exception_context:
            grpc.server(
                futures.ThreadPoolExecutor(max_workers=5),
                handlers=[
                    _ActualGenericRpcHandler(),
                    object(),
                ],
            )
        self.assertIn(
            "grpc.GenericRpcHandler", str(exception_context.exception)
        )

    def test_not_a_generic_rpc_handler_after_construction(self):
        server = grpc.server(futures.ThreadPoolExecutor(max_workers=5))
        with self.assertRaises(AttributeError) as exception_context:
            server.add_generic_rpc_handlers(
                [
                    _ActualGenericRpcHandler(),
                    object(),
                ]
            )
        self.assertIn(
            "grpc.GenericRpcHandler", str(exception_context.exception)
        )

    def test_failed_port_binding_exception(self):
        server = grpc.server(None, options=(("grpc.so_reuseport", 0),))
        port = server.add_insecure_port("localhost:0")
        bind_address = "localhost:%d" % port

        with self.assertRaises(RuntimeError):
            server.add_insecure_port(bind_address)

        server_credentials = grpc.ssl_server_credentials(
            [(resources.private_key(), resources.certificate_chain())]
        )
        with self.assertRaises(RuntimeError):
            server.add_secure_port(bind_address, server_credentials)


class ServerHandlerTest(unittest.TestCase):
    def tearDown(self):
        self._server.stop(0)
        self._channel.close()

    def test_generic_unary_unary_handler(self):
        self._server = test_common.test_server()
        port = self._server.add_insecure_port("[::]:0")
        self._server.start()
        self._server.add_generic_rpc_handlers((_GenericHandler(),))
        self._channel = grpc.insecure_channel("localhost:%d" % port)

        response = self._channel.unary_unary(
            _UNARY_UNARY,
            _registered_method=True,
        )(_REQUEST)
        self.assertEqual(_RESPONSE, response)

    def test_generic_unary_stream_handler(self):
        self._server = test_common.test_server()
        self._server.add_generic_rpc_handlers((_GenericHandler(),))
        port = self._server.add_insecure_port("[::]:0")
        self._server.start()
        self._channel = grpc.insecure_channel("localhost:%d" % port)

        response_iterator = self._channel.unary_stream(
            _UNARY_STREAM,
            _registered_method=True,
        )(_REQUEST)
        self.assertSequenceEqual(
            [_RESPONSE] * test_constants.STREAM_LENGTH, list(response_iterator)
        )

    def test_generic_stream_unary_handler(self):
        self._server = test_common.test_server()
        self._server.add_generic_rpc_handlers((_GenericHandler(),))
        port = self._server.add_insecure_port("[::]:0")
        self._server.start()
        self._channel = grpc.insecure_channel("localhost:%d" % port)

        response = self._channel.stream_unary(
            _STREAM_UNARY,
            _registered_method=True,
        )(iter([_REQUEST] * test_constants.STREAM_LENGTH))
        self.assertEqual(_RESPONSE, response)

    def test_generic_stream_stream_handler(self):
        self._server = test_common.test_server()
        self._server.add_generic_rpc_handlers((_GenericHandler(),))
        port = self._server.add_insecure_port("[::]:0")
        self._server.start()
        self._channel = grpc.insecure_channel("localhost:%d" % port)

        response_iterator = self._channel.stream_stream(
            _STREAM_STREAM,
            _registered_method=True,
        )(iter([_REQUEST] * test_constants.STREAM_LENGTH))
        self.assertSequenceEqual(
            [_RESPONSE] * test_constants.STREAM_LENGTH, list(response_iterator)
        )

    def test_add_generic_handler_after_server_start(self):
        self._server = test_common.test_server()
        port = self._server.add_insecure_port("[::]:0")
        self._server.start()
        self._server.add_generic_rpc_handlers((_GenericHandler(),))
        self._channel = grpc.insecure_channel("localhost:%d" % port)

        response = self._channel.unary_unary(
            _UNARY_UNARY,
            _registered_method=True,
        )(_REQUEST)
        self.assertEqual(_RESPONSE, response)

    def test_add_registered_handler_after_server_start(self):
        self._server = test_common.test_server()
        port = self._server.add_insecure_port("[::]:0")
        self._server.start()
        self._server.add_registered_method_handlers(
            _SERVICE_NAME, _REGISTERED_METHOD_HANDLERS
        )
        self._channel = grpc.insecure_channel("localhost:%d" % port)

        with self.assertRaises(grpc.RpcError) as exception_context:
            self._channel.unary_unary(
                grpc._common.fully_qualified_method(
                    _SERVICE_NAME, _UNARY_UNARY_REGISTERED
                ),
                _registered_method=True,
            )(_REQUEST)

        self.assertIn("Method not found", str(exception_context.exception))

    def test_server_with_both_registered_and_generic_handlers(self):
        self._server = test_common.test_server()
        self._server.add_generic_rpc_handlers((_GenericHandler(),))
        self._server.add_registered_method_handlers(
            _SERVICE_NAME, _REGISTERED_METHOD_HANDLERS
        )
        port = self._server.add_insecure_port("[::]:0")
        self._server.start()
        self._channel = grpc.insecure_channel("localhost:%d" % port)

        generic_response = self._channel.unary_unary(
            _UNARY_UNARY,
            _registered_method=True,
        )(_REQUEST)
        self.assertEqual(_RESPONSE, generic_response)

        registered_response = self._channel.unary_unary(
            grpc._common.fully_qualified_method(
                _SERVICE_NAME, _UNARY_UNARY_REGISTERED
            ),
            _registered_method=True,
        )(_REQUEST)
        self.assertEqual(_REGISTERED_RESPONSE, registered_response)

    def test_server_registered_handler_take_precedence(self):
        # Test if the same method have both generic and registered handler,
        # registered handler will take precedence.
        self._server = test_common.test_server()
        self._server.add_generic_rpc_handlers(
            (_GenericHandlerWithRegisteredName(),)
        )
        self._server.add_registered_method_handlers(
            _SERVICE_NAME, _REGISTERED_METHOD_HANDLERS
        )
        port = self._server.add_insecure_port("[::]:0")
        self._server.start()
        self._channel = grpc.insecure_channel("localhost:%d" % port)

        registered_response = self._channel.unary_unary(
            grpc._common.fully_qualified_method(
                _SERVICE_NAME, _UNARY_UNARY_REGISTERED
            ),
            _registered_method=True,
        )(_REQUEST)
        self.assertEqual(_REGISTERED_RESPONSE, registered_response)


if __name__ == "__main__":
    logging.basicConfig()
    unittest.main(verbosity=2)
