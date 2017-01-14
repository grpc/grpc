# Copyright 2015, Google Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

import argparse
import contextlib
import distutils.spawn
import errno
import itertools
import os
import pkg_resources
import shutil
import subprocess
import sys
import tempfile
import threading
import time
import unittest

from six import moves

from grpc.beta import implementations
from grpc.beta import interfaces
from grpc.framework.foundation import future
from grpc.framework.interfaces.face import face
from tests.unit.framework.common import test_constants

import tests.protoc_plugin.protos.payload.test_payload_pb2 as payload_pb2
import tests.protoc_plugin.protos.requests.r.test_requests_pb2 as request_pb2
import tests.protoc_plugin.protos.responses.test_responses_pb2 as response_pb2
import tests.protoc_plugin.protos.service.test_service_pb2 as service_pb2

# Identifiers of entities we expect to find in the generated module.
SERVICER_IDENTIFIER = 'BetaTestServiceServicer'
STUB_IDENTIFIER = 'BetaTestServiceStub'
SERVER_FACTORY_IDENTIFIER = 'beta_create_TestService_server'
STUB_FACTORY_IDENTIFIER = 'beta_create_TestService_stub'


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


@contextlib.contextmanager
def _CreateService():
    """Provides a servicer backend and a stub.

  The servicer is just the implementation of the actual servicer passed to the
  face player of the python RPC implementation; the two are detached.

  Yields:
    A (servicer_methods, stub) pair where servicer_methods is the back-end of
      the service bound to the stub and and stub is the stub on which to invoke
      RPCs.
  """
    servicer_methods = _ServicerMethods()

    class Servicer(getattr(service_pb2, SERVICER_IDENTIFIER)):

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

    servicer = Servicer()
    server = getattr(service_pb2, SERVER_FACTORY_IDENTIFIER)(servicer)
    port = server.add_insecure_port('[::]:0')
    server.start()
    channel = implementations.insecure_channel('localhost', port)
    stub = getattr(service_pb2, STUB_FACTORY_IDENTIFIER)(channel)
    yield (servicer_methods, stub)
    server.stop(0)


@contextlib.contextmanager
def _CreateIncompleteService():
    """Provides a servicer backend that fails to implement methods and its stub.

  The servicer is just the implementation of the actual servicer passed to the
  face player of the python RPC implementation; the two are detached.
  Args:
    service_pb2: The service_pb2 module generated by this test.
  Yields:
    A (servicer_methods, stub) pair where servicer_methods is the back-end of
      the service bound to the stub and and stub is the stub on which to invoke
      RPCs.
  """

    class Servicer(getattr(service_pb2, SERVICER_IDENTIFIER)):
        pass

    servicer = Servicer()
    server = getattr(service_pb2, SERVER_FACTORY_IDENTIFIER)(servicer)
    port = server.add_insecure_port('[::]:0')
    server.start()
    channel = implementations.insecure_channel('localhost', port)
    stub = getattr(service_pb2, STUB_FACTORY_IDENTIFIER)(channel)
    yield None, stub
    server.stop(0)


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
        self.assertIsNotNone(getattr(service_pb2, SERVICER_IDENTIFIER, None))
        self.assertIsNotNone(getattr(service_pb2, STUB_IDENTIFIER, None))
        self.assertIsNotNone(
            getattr(service_pb2, SERVER_FACTORY_IDENTIFIER, None))
        self.assertIsNotNone(
            getattr(service_pb2, STUB_FACTORY_IDENTIFIER, None))

    def testUpDown(self):
        with _CreateService():
            request_pb2.SimpleRequest(response_size=13)

    def testIncompleteServicer(self):
        with _CreateIncompleteService() as (_, stub):
            request = request_pb2.SimpleRequest(response_size=13)
            try:
                stub.UnaryCall(request, test_constants.LONG_TIMEOUT)
            except face.AbortionError as error:
                self.assertEqual(interfaces.StatusCode.UNIMPLEMENTED,
                                 error.code)

    def testUnaryCall(self):
        with _CreateService() as (methods, stub):
            request = request_pb2.SimpleRequest(response_size=13)
            response = stub.UnaryCall(request, test_constants.LONG_TIMEOUT)
        expected_response = methods.UnaryCall(request, 'not a real context!')
        self.assertEqual(expected_response, response)

    def testUnaryCallFuture(self):
        with _CreateService() as (methods, stub):
            request = request_pb2.SimpleRequest(response_size=13)
            # Check that the call does not block waiting for the server to respond.
            with methods.pause():
                response_future = stub.UnaryCall.future(
                    request, test_constants.LONG_TIMEOUT)
            response = response_future.result()
        expected_response = methods.UnaryCall(request, 'not a real RpcContext!')
        self.assertEqual(expected_response, response)

    def testUnaryCallFutureExpired(self):
        with _CreateService() as (methods, stub):
            request = request_pb2.SimpleRequest(response_size=13)
            with methods.pause():
                response_future = stub.UnaryCall.future(
                    request, test_constants.SHORT_TIMEOUT)
                with self.assertRaises(face.ExpirationError):
                    response_future.result()

    def testUnaryCallFutureCancelled(self):
        with _CreateService() as (methods, stub):
            request = request_pb2.SimpleRequest(response_size=13)
            with methods.pause():
                response_future = stub.UnaryCall.future(request, 1)
                response_future.cancel()
                self.assertTrue(response_future.cancelled())

    def testUnaryCallFutureFailed(self):
        with _CreateService() as (methods, stub):
            request = request_pb2.SimpleRequest(response_size=13)
            with methods.fail():
                response_future = stub.UnaryCall.future(
                    request, test_constants.LONG_TIMEOUT)
                self.assertIsNotNone(response_future.exception())

    def testStreamingOutputCall(self):
        with _CreateService() as (methods, stub):
            request = _streaming_output_request()
            responses = stub.StreamingOutputCall(request,
                                                 test_constants.LONG_TIMEOUT)
            expected_responses = methods.StreamingOutputCall(
                request, 'not a real RpcContext!')
            for expected_response, response in moves.zip_longest(
                    expected_responses, responses):
                self.assertEqual(expected_response, response)

    def testStreamingOutputCallExpired(self):
        with _CreateService() as (methods, stub):
            request = _streaming_output_request()
            with methods.pause():
                responses = stub.StreamingOutputCall(
                    request, test_constants.SHORT_TIMEOUT)
                with self.assertRaises(face.ExpirationError):
                    list(responses)

    def testStreamingOutputCallCancelled(self):
        with _CreateService() as (methods, stub):
            request = _streaming_output_request()
            responses = stub.StreamingOutputCall(request,
                                                 test_constants.LONG_TIMEOUT)
            next(responses)
            responses.cancel()
            with self.assertRaises(face.CancellationError):
                next(responses)

    def testStreamingOutputCallFailed(self):
        with _CreateService() as (methods, stub):
            request = _streaming_output_request()
            with methods.fail():
                responses = stub.StreamingOutputCall(request, 1)
                self.assertIsNotNone(responses)
                with self.assertRaises(face.RemoteError):
                    next(responses)

    def testStreamingInputCall(self):
        with _CreateService() as (methods, stub):
            response = stub.StreamingInputCall(
                _streaming_input_request_iterator(),
                test_constants.LONG_TIMEOUT)
        expected_response = methods.StreamingInputCall(
            _streaming_input_request_iterator(), 'not a real RpcContext!')
        self.assertEqual(expected_response, response)

    def testStreamingInputCallFuture(self):
        with _CreateService() as (methods, stub):
            with methods.pause():
                response_future = stub.StreamingInputCall.future(
                    _streaming_input_request_iterator(),
                    test_constants.LONG_TIMEOUT)
            response = response_future.result()
        expected_response = methods.StreamingInputCall(
            _streaming_input_request_iterator(), 'not a real RpcContext!')
        self.assertEqual(expected_response, response)

    def testStreamingInputCallFutureExpired(self):
        with _CreateService() as (methods, stub):
            with methods.pause():
                response_future = stub.StreamingInputCall.future(
                    _streaming_input_request_iterator(),
                    test_constants.SHORT_TIMEOUT)
                with self.assertRaises(face.ExpirationError):
                    response_future.result()
                self.assertIsInstance(response_future.exception(),
                                      face.ExpirationError)

    def testStreamingInputCallFutureCancelled(self):
        with _CreateService() as (methods, stub):
            with methods.pause():
                response_future = stub.StreamingInputCall.future(
                    _streaming_input_request_iterator(),
                    test_constants.LONG_TIMEOUT)
                response_future.cancel()
                self.assertTrue(response_future.cancelled())
            with self.assertRaises(future.CancelledError):
                response_future.result()

    def testStreamingInputCallFutureFailed(self):
        with _CreateService() as (methods, stub):
            with methods.fail():
                response_future = stub.StreamingInputCall.future(
                    _streaming_input_request_iterator(),
                    test_constants.LONG_TIMEOUT)
                self.assertIsNotNone(response_future.exception())

    def testFullDuplexCall(self):
        with _CreateService() as (methods, stub):
            responses = stub.FullDuplexCall(_full_duplex_request_iterator(),
                                            test_constants.LONG_TIMEOUT)
            expected_responses = methods.FullDuplexCall(
                _full_duplex_request_iterator(), 'not a real RpcContext!')
            for expected_response, response in moves.zip_longest(
                    expected_responses, responses):
                self.assertEqual(expected_response, response)

    def testFullDuplexCallExpired(self):
        request_iterator = _full_duplex_request_iterator()
        with _CreateService() as (methods, stub):
            with methods.pause():
                responses = stub.FullDuplexCall(request_iterator,
                                                test_constants.SHORT_TIMEOUT)
                with self.assertRaises(face.ExpirationError):
                    list(responses)

    def testFullDuplexCallCancelled(self):
        with _CreateService() as (methods, stub):
            request_iterator = _full_duplex_request_iterator()
            responses = stub.FullDuplexCall(request_iterator,
                                            test_constants.LONG_TIMEOUT)
            next(responses)
            responses.cancel()
            with self.assertRaises(face.CancellationError):
                next(responses)

    def testFullDuplexCallFailed(self):
        request_iterator = _full_duplex_request_iterator()
        with _CreateService() as (methods, stub):
            with methods.fail():
                responses = stub.FullDuplexCall(request_iterator,
                                                test_constants.LONG_TIMEOUT)
                self.assertIsNotNone(responses)
                with self.assertRaises(face.RemoteError):
                    next(responses)

    def testHalfDuplexCall(self):
        with _CreateService() as (methods, stub):

            def half_duplex_request_iterator():
                request = request_pb2.StreamingOutputCallRequest()
                request.response_parameters.add(size=1, interval_us=0)
                yield request
                request = request_pb2.StreamingOutputCallRequest()
                request.response_parameters.add(size=2, interval_us=0)
                request.response_parameters.add(size=3, interval_us=0)
                yield request

            responses = stub.HalfDuplexCall(half_duplex_request_iterator(),
                                            test_constants.LONG_TIMEOUT)
            expected_responses = methods.HalfDuplexCall(
                half_duplex_request_iterator(), 'not a real RpcContext!')
            for check in moves.zip_longest(expected_responses, responses):
                expected_response, response = check
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

        with _CreateService() as (methods, stub):
            with wait():
                responses = stub.HalfDuplexCall(half_duplex_request_iterator(),
                                                test_constants.SHORT_TIMEOUT)
                # half-duplex waits for the client to send all info
                with self.assertRaises(face.ExpirationError):
                    next(responses)


if __name__ == '__main__':
    unittest.main(verbosity=2)
