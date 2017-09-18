# Copyright 2015 gRPC authors.
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

import collections
from concurrent import futures
import contextlib
import distutils.spawn
import errno
import os
import shutil
import subprocess
import sys
import tempfile
import threading
import unittest

from six import moves

import grpc
from tests.unit.framework.common import test_constants

import tests.protoc_plugin.protos.payload.test_payload_pb2 as payload_pb2
import tests.protoc_plugin.protos.requests.r.test_requests_pb2 as request_pb2
import tests.protoc_plugin.protos.responses.test_responses_pb2 as response_pb2
import tests.protoc_plugin.protos.service.test_service_pb2_grpc as service_pb2_grpc

# Identifiers of entities we expect to find in the generated module.
STUB_IDENTIFIER = 'TestServiceStub'
SERVICER_IDENTIFIER = 'TestServiceServicer'
ADD_SERVICER_TO_SERVER_IDENTIFIER = 'add_TestServiceServicer_to_server'


class _ServicerMethods(object):

    def __init__(self):
        self._condition = threading.Condition()
        self._paused = False
        self._fail = False

    @contextlib.contextmanager
    def pause(self):  # pylint: disable=invalid-name
        with self._condition:
            self._paused = True
        yield
        with self._condition:
            self._paused = False
            self._condition.notify_all()

    @contextlib.contextmanager
    def fail(self):  # pylint: disable=invalid-name
        with self._condition:
            self._fail = True
        yield
        with self._condition:
            self._fail = False

    def _control(self):  # pylint: disable=invalid-name
        with self._condition:
            if self._fail:
                raise ValueError()
            while self._paused:
                self._condition.wait()

    def UnaryCall(self, request, unused_rpc_context):
        response = response_pb2.SimpleResponse()
        response.payload.payload_type = payload_pb2.COMPRESSABLE
        response.payload.payload_compressable = 'a' * request.response_size
        self._control()
        return response

    def StreamingOutputCall(self, request, unused_rpc_context):
        for parameter in request.response_parameters:
            response = response_pb2.StreamingOutputCallResponse()
            response.payload.payload_type = payload_pb2.COMPRESSABLE
            response.payload.payload_compressable = 'a' * parameter.size
            self._control()
            yield response

    def StreamingInputCall(self, request_iter, unused_rpc_context):
        response = response_pb2.StreamingInputCallResponse()
        aggregated_payload_size = 0
        for request in request_iter:
            aggregated_payload_size += len(request.payload.payload_compressable)
        response.aggregated_payload_size = aggregated_payload_size
        self._control()
        return response

    def FullDuplexCall(self, request_iter, unused_rpc_context):
        for request in request_iter:
            for parameter in request.response_parameters:
                response = response_pb2.StreamingOutputCallResponse()
                response.payload.payload_type = payload_pb2.COMPRESSABLE
                response.payload.payload_compressable = 'a' * parameter.size
                self._control()
                yield response

    def HalfDuplexCall(self, request_iter, unused_rpc_context):
        responses = []
        for request in request_iter:
            for parameter in request.response_parameters:
                response = response_pb2.StreamingOutputCallResponse()
                response.payload.payload_type = payload_pb2.COMPRESSABLE
                response.payload.payload_compressable = 'a' * parameter.size
                self._control()
                responses.append(response)
        for response in responses:
            yield response


class _Service(
        collections.namedtuple('_Service', ('servicer_methods', 'server',
                                            'stub',))):
    """A live and running service.

  Attributes:
    servicer_methods: The _ServicerMethods servicing RPCs.
    server: The grpc.Server servicing RPCs.
    stub: A stub on which to invoke RPCs.
  """


def _CreateService():
    """Provides a servicer backend and a stub.

  Returns:
    A _Service with which to test RPCs.
  """
    servicer_methods = _ServicerMethods()

    class Servicer(getattr(service_pb2_grpc, SERVICER_IDENTIFIER)):

        def UnaryCall(self, request, context):
            return servicer_methods.UnaryCall(request, context)

        def StreamingOutputCall(self, request, context):
            return servicer_methods.StreamingOutputCall(request, context)

        def StreamingInputCall(self, request_iter, context):
            return servicer_methods.StreamingInputCall(request_iter, context)

        def FullDuplexCall(self, request_iter, context):
            return servicer_methods.FullDuplexCall(request_iter, context)

        def HalfDuplexCall(self, request_iter, context):
            return servicer_methods.HalfDuplexCall(request_iter, context)

    server = grpc.server(
        futures.ThreadPoolExecutor(max_workers=test_constants.POOL_SIZE))
    getattr(service_pb2_grpc, ADD_SERVICER_TO_SERVER_IDENTIFIER)(Servicer(),
                                                                 server)
    port = server.add_insecure_port('[::]:0')
    server.start()
    channel = grpc.insecure_channel('localhost:{}'.format(port))
    stub = getattr(service_pb2_grpc, STUB_IDENTIFIER)(channel)
    return _Service(servicer_methods, server, stub)


def _CreateIncompleteService():
    """Provides a servicer backend that fails to implement methods and its stub.

  Returns:
    A _Service with which to test RPCs. The returned _Service's
      servicer_methods implements none of the methods required of it.
  """

    class Servicer(getattr(service_pb2_grpc, SERVICER_IDENTIFIER)):
        pass

    server = grpc.server(
        futures.ThreadPoolExecutor(max_workers=test_constants.POOL_SIZE))
    getattr(service_pb2_grpc, ADD_SERVICER_TO_SERVER_IDENTIFIER)(Servicer(),
                                                                 server)
    port = server.add_insecure_port('[::]:0')
    server.start()
    channel = grpc.insecure_channel('localhost:{}'.format(port))
    stub = getattr(service_pb2_grpc, STUB_IDENTIFIER)(channel)
    return _Service(None, server, stub)


def _streaming_input_request_iterator():
    for _ in range(3):
        request = request_pb2.StreamingInputCallRequest()
        request.payload.payload_type = payload_pb2.COMPRESSABLE
        request.payload.payload_compressable = 'a'
        yield request


def _streaming_output_request():
    request = request_pb2.StreamingOutputCallRequest()
    sizes = [1, 2, 3]
    request.response_parameters.add(size=sizes[0], interval_us=0)
    request.response_parameters.add(size=sizes[1], interval_us=0)
    request.response_parameters.add(size=sizes[2], interval_us=0)
    return request


def _full_duplex_request_iterator():
    request = request_pb2.StreamingOutputCallRequest()
    request.response_parameters.add(size=1, interval_us=0)
    yield request
    request = request_pb2.StreamingOutputCallRequest()
    request.response_parameters.add(size=2, interval_us=0)
    request.response_parameters.add(size=3, interval_us=0)
    yield request


class PythonPluginTest(unittest.TestCase):
    """Test case for the gRPC Python protoc-plugin.

  While reading these tests, remember that the futures API
  (`stub.method.future()`) only gives futures for the *response-unary*
  methods and does not exist for response-streaming methods.
  """

    def testImportAttributes(self):
        # check that we can access the generated module and its members.
        self.assertIsNotNone(getattr(service_pb2_grpc, STUB_IDENTIFIER, None))
        self.assertIsNotNone(
            getattr(service_pb2_grpc, SERVICER_IDENTIFIER, None))
        self.assertIsNotNone(
            getattr(service_pb2_grpc, ADD_SERVICER_TO_SERVER_IDENTIFIER, None))

    def testUpDown(self):
        service = _CreateService()
        self.assertIsNotNone(service.servicer_methods)
        self.assertIsNotNone(service.server)
        self.assertIsNotNone(service.stub)

    def testIncompleteServicer(self):
        service = _CreateIncompleteService()
        request = request_pb2.SimpleRequest(response_size=13)
        with self.assertRaises(grpc.RpcError) as exception_context:
            service.stub.UnaryCall(request)
        self.assertIs(exception_context.exception.code(),
                      grpc.StatusCode.UNIMPLEMENTED)

    def testUnaryCall(self):
        service = _CreateService()
        request = request_pb2.SimpleRequest(response_size=13)
        response = service.stub.UnaryCall(request)
        expected_response = service.servicer_methods.UnaryCall(
            request, 'not a real context!')
        self.assertEqual(expected_response, response)

    def testUnaryCallFuture(self):
        service = _CreateService()
        request = request_pb2.SimpleRequest(response_size=13)
        # Check that the call does not block waiting for the server to respond.
        with service.servicer_methods.pause():
            response_future = service.stub.UnaryCall.future(request)
        response = response_future.result()
        expected_response = service.servicer_methods.UnaryCall(
            request, 'not a real RpcContext!')
        self.assertEqual(expected_response, response)

    def testUnaryCallFutureExpired(self):
        service = _CreateService()
        request = request_pb2.SimpleRequest(response_size=13)
        with service.servicer_methods.pause():
            response_future = service.stub.UnaryCall.future(
                request, timeout=test_constants.SHORT_TIMEOUT)
            with self.assertRaises(grpc.RpcError) as exception_context:
                response_future.result()
        self.assertIs(exception_context.exception.code(),
                      grpc.StatusCode.DEADLINE_EXCEEDED)
        self.assertIs(response_future.code(), grpc.StatusCode.DEADLINE_EXCEEDED)

    def testUnaryCallFutureCancelled(self):
        service = _CreateService()
        request = request_pb2.SimpleRequest(response_size=13)
        with service.servicer_methods.pause():
            response_future = service.stub.UnaryCall.future(request)
            response_future.cancel()
        self.assertTrue(response_future.cancelled())
        self.assertIs(response_future.code(), grpc.StatusCode.CANCELLED)

    def testUnaryCallFutureFailed(self):
        service = _CreateService()
        request = request_pb2.SimpleRequest(response_size=13)
        with service.servicer_methods.fail():
            response_future = service.stub.UnaryCall.future(request)
            self.assertIsNotNone(response_future.exception())
        self.assertIs(response_future.code(), grpc.StatusCode.UNKNOWN)

    def testStreamingOutputCall(self):
        service = _CreateService()
        request = _streaming_output_request()
        responses = service.stub.StreamingOutputCall(request)
        expected_responses = service.servicer_methods.StreamingOutputCall(
            request, 'not a real RpcContext!')
        for expected_response, response in moves.zip_longest(expected_responses,
                                                             responses):
            self.assertEqual(expected_response, response)

    def testStreamingOutputCallExpired(self):
        service = _CreateService()
        request = _streaming_output_request()
        with service.servicer_methods.pause():
            responses = service.stub.StreamingOutputCall(
                request, timeout=test_constants.SHORT_TIMEOUT)
            with self.assertRaises(grpc.RpcError) as exception_context:
                list(responses)
        self.assertIs(exception_context.exception.code(),
                      grpc.StatusCode.DEADLINE_EXCEEDED)

    def testStreamingOutputCallCancelled(self):
        service = _CreateService()
        request = _streaming_output_request()
        responses = service.stub.StreamingOutputCall(request)
        next(responses)
        responses.cancel()
        with self.assertRaises(grpc.RpcError) as exception_context:
            next(responses)
        self.assertIs(responses.code(), grpc.StatusCode.CANCELLED)

    def testStreamingOutputCallFailed(self):
        service = _CreateService()
        request = _streaming_output_request()
        with service.servicer_methods.fail():
            responses = service.stub.StreamingOutputCall(request)
            self.assertIsNotNone(responses)
            with self.assertRaises(grpc.RpcError) as exception_context:
                next(responses)
        self.assertIs(exception_context.exception.code(),
                      grpc.StatusCode.UNKNOWN)

    def testStreamingInputCall(self):
        service = _CreateService()
        response = service.stub.StreamingInputCall(
            _streaming_input_request_iterator())
        expected_response = service.servicer_methods.StreamingInputCall(
            _streaming_input_request_iterator(), 'not a real RpcContext!')
        self.assertEqual(expected_response, response)

    def testStreamingInputCallFuture(self):
        service = _CreateService()
        with service.servicer_methods.pause():
            response_future = service.stub.StreamingInputCall.future(
                _streaming_input_request_iterator())
        response = response_future.result()
        expected_response = service.servicer_methods.StreamingInputCall(
            _streaming_input_request_iterator(), 'not a real RpcContext!')
        self.assertEqual(expected_response, response)

    def testStreamingInputCallFutureExpired(self):
        service = _CreateService()
        with service.servicer_methods.pause():
            response_future = service.stub.StreamingInputCall.future(
                _streaming_input_request_iterator(),
                timeout=test_constants.SHORT_TIMEOUT)
            with self.assertRaises(grpc.RpcError) as exception_context:
                response_future.result()
        self.assertIsInstance(response_future.exception(), grpc.RpcError)
        self.assertIs(response_future.exception().code(),
                      grpc.StatusCode.DEADLINE_EXCEEDED)
        self.assertIs(exception_context.exception.code(),
                      grpc.StatusCode.DEADLINE_EXCEEDED)

    def testStreamingInputCallFutureCancelled(self):
        service = _CreateService()
        with service.servicer_methods.pause():
            response_future = service.stub.StreamingInputCall.future(
                _streaming_input_request_iterator())
            response_future.cancel()
        self.assertTrue(response_future.cancelled())
        with self.assertRaises(grpc.FutureCancelledError):
            response_future.result()

    def testStreamingInputCallFutureFailed(self):
        service = _CreateService()
        with service.servicer_methods.fail():
            response_future = service.stub.StreamingInputCall.future(
                _streaming_input_request_iterator())
            self.assertIsNotNone(response_future.exception())
            self.assertIs(response_future.code(), grpc.StatusCode.UNKNOWN)

    def testFullDuplexCall(self):
        service = _CreateService()
        responses = service.stub.FullDuplexCall(_full_duplex_request_iterator())
        expected_responses = service.servicer_methods.FullDuplexCall(
            _full_duplex_request_iterator(), 'not a real RpcContext!')
        for expected_response, response in moves.zip_longest(expected_responses,
                                                             responses):
            self.assertEqual(expected_response, response)

    def testFullDuplexCallExpired(self):
        request_iterator = _full_duplex_request_iterator()
        service = _CreateService()
        with service.servicer_methods.pause():
            responses = service.stub.FullDuplexCall(
                request_iterator, timeout=test_constants.SHORT_TIMEOUT)
            with self.assertRaises(grpc.RpcError) as exception_context:
                list(responses)
        self.assertIs(exception_context.exception.code(),
                      grpc.StatusCode.DEADLINE_EXCEEDED)

    def testFullDuplexCallCancelled(self):
        service = _CreateService()
        request_iterator = _full_duplex_request_iterator()
        responses = service.stub.FullDuplexCall(request_iterator)
        next(responses)
        responses.cancel()
        with self.assertRaises(grpc.RpcError) as exception_context:
            next(responses)
        self.assertIs(exception_context.exception.code(),
                      grpc.StatusCode.CANCELLED)

    def testFullDuplexCallFailed(self):
        request_iterator = _full_duplex_request_iterator()
        service = _CreateService()
        with service.servicer_methods.fail():
            responses = service.stub.FullDuplexCall(request_iterator)
            with self.assertRaises(grpc.RpcError) as exception_context:
                next(responses)
        self.assertIs(exception_context.exception.code(),
                      grpc.StatusCode.UNKNOWN)

    def testHalfDuplexCall(self):
        service = _CreateService()

        def half_duplex_request_iterator():
            request = request_pb2.StreamingOutputCallRequest()
            request.response_parameters.add(size=1, interval_us=0)
            yield request
            request = request_pb2.StreamingOutputCallRequest()
            request.response_parameters.add(size=2, interval_us=0)
            request.response_parameters.add(size=3, interval_us=0)
            yield request

        responses = service.stub.HalfDuplexCall(half_duplex_request_iterator())
        expected_responses = service.servicer_methods.HalfDuplexCall(
            half_duplex_request_iterator(), 'not a real RpcContext!')
        for expected_response, response in moves.zip_longest(expected_responses,
                                                             responses):
            self.assertEqual(expected_response, response)

    def testHalfDuplexCallWedged(self):
        condition = threading.Condition()
        wait_cell = [False]

        @contextlib.contextmanager
        def wait():  # pylint: disable=invalid-name
            # Where's Python 3's 'nonlocal' statement when you need it?
            with condition:
                wait_cell[0] = True
            yield
            with condition:
                wait_cell[0] = False
                condition.notify_all()

        def half_duplex_request_iterator():
            request = request_pb2.StreamingOutputCallRequest()
            request.response_parameters.add(size=1, interval_us=0)
            yield request
            with condition:
                while wait_cell[0]:
                    condition.wait()

        service = _CreateService()
        with wait():
            responses = service.stub.HalfDuplexCall(
                half_duplex_request_iterator(),
                timeout=test_constants.SHORT_TIMEOUT)
            # half-duplex waits for the client to send all info
            with self.assertRaises(grpc.RpcError) as exception_context:
                next(responses)
        self.assertIs(exception_context.exception.code(),
                      grpc.StatusCode.DEADLINE_EXCEEDED)


if __name__ == '__main__':
    unittest.main(verbosity=2)
