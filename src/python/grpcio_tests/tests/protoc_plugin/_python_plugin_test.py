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
import contextlib
import distutils.spawn
import errno
import itertools
import os
import shutil
import subprocess
import sys
import tempfile
import threading
import unittest

import grpc
import grpc.experimental

import tests.protoc_plugin.protos.payload.test_payload_pb2 as payload_pb2
import tests.protoc_plugin.protos.requests.r.test_requests_pb2 as request_pb2
import tests.protoc_plugin.protos.responses.test_responses_pb2 as response_pb2
import tests.protoc_plugin.protos.service.test_service_pb2_grpc as service_pb2_grpc
from tests.unit import test_common
from tests.unit.framework.common import test_constants

# Identifiers of entities we expect to find in the generated module.
STUB_IDENTIFIER = "TestServiceStub"
SERVICER_IDENTIFIER = "TestServiceServicer"
ADD_SERVICER_TO_SERVER_IDENTIFIER = "add_TestServiceServicer_to_server"


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
        response.payload.payload_compressable = "a" * request.response_size
        self._control()
        return response

    def StreamingOutputCall(self, request, unused_rpc_context):
        for parameter in request.response_parameters:
            response = response_pb2.StreamingOutputCallResponse()
            response.payload.payload_type = payload_pb2.COMPRESSABLE
            response.payload.payload_compressable = "a" * parameter.size
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
                response.payload.payload_compressable = "a" * parameter.size
                self._control()
                yield response

    def HalfDuplexCall(self, request_iter, unused_rpc_context):
        responses = []
        for request in request_iter:
            for parameter in request.response_parameters:
                response = response_pb2.StreamingOutputCallResponse()
                response.payload.payload_type = payload_pb2.COMPRESSABLE
                response.payload.payload_compressable = "a" * parameter.size
                self._control()
                responses.append(response)
        for response in responses:
            yield response


class _Service(
    collections.namedtuple(
        "_Service",
        (
            "servicer_methods",
            "server",
            "stub",
        ),
    )
):
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

        def StreamingInputCall(self, request_iterator, context):
            return servicer_methods.StreamingInputCall(
                request_iterator, context
            )

        def FullDuplexCall(self, request_iterator, context):
            return servicer_methods.FullDuplexCall(request_iterator, context)

        def HalfDuplexCall(self, request_iterator, context):
            return servicer_methods.HalfDuplexCall(request_iterator, context)

    server = test_common.test_server()
    getattr(service_pb2_grpc, ADD_SERVICER_TO_SERVER_IDENTIFIER)(
        Servicer(), server
    )
    port = server.add_insecure_port("[::]:0")
    server.start()
    channel = grpc.insecure_channel(f"localhost:{port}")
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

    server = test_common.test_server()
    getattr(service_pb2_grpc, ADD_SERVICER_TO_SERVER_IDENTIFIER)(
        Servicer(), server
    )
    port = server.add_insecure_port("[::]:0")
    server.start()
    channel = grpc.insecure_channel(f"localhost:{port}")
    stub = getattr(service_pb2_grpc, STUB_IDENTIFIER)(channel)
    return _Service(None, server, stub)


def _streaming_input_request_iterator():
    for _ in range(3):
        request = request_pb2.StreamingInputCallRequest()
        request.payload.payload_type = payload_pb2.COMPRESSABLE
        request.payload.payload_compressable = "a"
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
            getattr(service_pb2_grpc, SERVICER_IDENTIFIER, None)
        )
        self.assertIsNotNone(
            getattr(service_pb2_grpc, ADD_SERVICER_TO_SERVER_IDENTIFIER, None)
        )

    def testUpDown(self):
        service = _CreateService()
        self.assertIsNotNone(service.servicer_methods)
        self.assertIsNotNone(service.server)
        self.assertIsNotNone(service.stub)
        service.server.stop(None)

    def testIncompleteServicer(self):
        service = _CreateIncompleteService()
        request = request_pb2.SimpleRequest(response_size=13)
        with self.assertRaises(grpc.RpcError) as exception_context:
            service.stub.UnaryCall(request)
        self.assertIs(
            exception_context.exception.code(), grpc.StatusCode.UNIMPLEMENTED
        )
        service.server.stop(None)

    def testUnaryCall(self):
        service = _CreateService()
        request = request_pb2.SimpleRequest(response_size=13)
        response = service.stub.UnaryCall(request)
        expected_response = service.servicer_methods.UnaryCall(
            request, "not a real context!"
        )
        self.assertEqual(expected_response, response)
        service.server.stop(None)

    def testUnaryCallFuture(self):
        service = _CreateService()
        request = request_pb2.SimpleRequest(response_size=13)
        # Check that the call does not block waiting for the server to respond.
        with service.servicer_methods.pause():
            response_future = service.stub.UnaryCall.future(request)
        response = response_future.result()
        expected_response = service.servicer_methods.UnaryCall(
            request, "not a real RpcContext!"
        )
        self.assertEqual(expected_response, response)
        service.server.stop(None)

    def testUnaryCallFutureExpired(self):
        service = _CreateService()
        request = request_pb2.SimpleRequest(response_size=13)
        with service.servicer_methods.pause():
            response_future = service.stub.UnaryCall.future(
                request, timeout=test_constants.SHORT_TIMEOUT
            )
            with self.assertRaises(grpc.RpcError) as exception_context:
                response_future.result()
        self.assertIs(
            exception_context.exception.code(),
            grpc.StatusCode.DEADLINE_EXCEEDED,
        )
        self.assertIs(response_future.code(), grpc.StatusCode.DEADLINE_EXCEEDED)
        service.server.stop(None)

    def testUnaryCallFutureCancelled(self):
        service = _CreateService()
        request = request_pb2.SimpleRequest(response_size=13)
        with service.servicer_methods.pause():
            response_future = service.stub.UnaryCall.future(request)
            response_future.cancel()
        self.assertTrue(response_future.cancelled())
        self.assertIs(response_future.code(), grpc.StatusCode.CANCELLED)
        service.server.stop(None)

    def testUnaryCallFutureFailed(self):
        service = _CreateService()
        request = request_pb2.SimpleRequest(response_size=13)
        with service.servicer_methods.fail():
            response_future = service.stub.UnaryCall.future(request)
            self.assertIsNotNone(response_future.exception())
        self.assertIs(response_future.code(), grpc.StatusCode.UNKNOWN)
        service.server.stop(None)

    def testStreamingOutputCall(self):
        service = _CreateService()
        request = _streaming_output_request()
        responses = service.stub.StreamingOutputCall(request)
        expected_responses = service.servicer_methods.StreamingOutputCall(
            request, "not a real RpcContext!"
        )
        for expected_response, response in itertools.zip_longest(
            expected_responses, responses
        ):
            self.assertEqual(expected_response, response)
        service.server.stop(None)

    def testStreamingOutputCallExpired(self):
        service = _CreateService()
        request = _streaming_output_request()
        with service.servicer_methods.pause():
            responses = service.stub.StreamingOutputCall(
                request, timeout=test_constants.SHORT_TIMEOUT
            )
            with self.assertRaises(grpc.RpcError) as exception_context:
                list(responses)
        self.assertIs(
            exception_context.exception.code(),
            grpc.StatusCode.DEADLINE_EXCEEDED,
        )
        service.server.stop(None)

    def testStreamingOutputCallCancelled(self):
        service = _CreateService()
        request = _streaming_output_request()
        responses = service.stub.StreamingOutputCall(request)
        next(responses)
        responses.cancel()
        with self.assertRaises(grpc.RpcError) as exception_context:
            next(responses)
        self.assertIs(responses.code(), grpc.StatusCode.CANCELLED)
        service.server.stop(None)

    def testStreamingOutputCallFailed(self):
        service = _CreateService()
        request = _streaming_output_request()
        with service.servicer_methods.fail():
            responses = service.stub.StreamingOutputCall(request)
            self.assertIsNotNone(responses)
            with self.assertRaises(grpc.RpcError) as exception_context:
                next(responses)
        self.assertIs(
            exception_context.exception.code(), grpc.StatusCode.UNKNOWN
        )
        service.server.stop(None)

    def testStreamingInputCall(self):
        service = _CreateService()
        response = service.stub.StreamingInputCall(
            _streaming_input_request_iterator()
        )
        expected_response = service.servicer_methods.StreamingInputCall(
            _streaming_input_request_iterator(), "not a real RpcContext!"
        )
        self.assertEqual(expected_response, response)
        service.server.stop(None)

    def testStreamingInputCallFuture(self):
        service = _CreateService()
        with service.servicer_methods.pause():
            response_future = service.stub.StreamingInputCall.future(
                _streaming_input_request_iterator()
            )
        response = response_future.result()
        expected_response = service.servicer_methods.StreamingInputCall(
            _streaming_input_request_iterator(), "not a real RpcContext!"
        )
        self.assertEqual(expected_response, response)
        service.server.stop(None)

    def testStreamingInputCallFutureExpired(self):
        service = _CreateService()
        with service.servicer_methods.pause():
            response_future = service.stub.StreamingInputCall.future(
                _streaming_input_request_iterator(),
                timeout=test_constants.SHORT_TIMEOUT,
            )
            with self.assertRaises(grpc.RpcError) as exception_context:
                response_future.result()
        self.assertIsInstance(response_future.exception(), grpc.RpcError)
        self.assertIs(
            response_future.exception().code(),
            grpc.StatusCode.DEADLINE_EXCEEDED,
        )
        self.assertIs(
            exception_context.exception.code(),
            grpc.StatusCode.DEADLINE_EXCEEDED,
        )
        service.server.stop(None)

    def testStreamingInputCallFutureCancelled(self):
        service = _CreateService()
        with service.servicer_methods.pause():
            response_future = service.stub.StreamingInputCall.future(
                _streaming_input_request_iterator()
            )
            response_future.cancel()
        self.assertTrue(response_future.cancelled())
        with self.assertRaises(grpc.FutureCancelledError):
            response_future.result()
        service.server.stop(None)

    def testStreamingInputCallFutureFailed(self):
        service = _CreateService()
        with service.servicer_methods.fail():
            response_future = service.stub.StreamingInputCall.future(
                _streaming_input_request_iterator()
            )
            self.assertIsNotNone(response_future.exception())
            self.assertIs(response_future.code(), grpc.StatusCode.UNKNOWN)
        service.server.stop(None)

    def testFullDuplexCall(self):
        service = _CreateService()
        responses = service.stub.FullDuplexCall(_full_duplex_request_iterator())
        expected_responses = service.servicer_methods.FullDuplexCall(
            _full_duplex_request_iterator(), "not a real RpcContext!"
        )
        for expected_response, response in itertools.zip_longest(
            expected_responses, responses
        ):
            self.assertEqual(expected_response, response)
        service.server.stop(None)

    def testFullDuplexCallExpired(self):
        request_iterator = _full_duplex_request_iterator()
        service = _CreateService()
        with service.servicer_methods.pause():
            responses = service.stub.FullDuplexCall(
                request_iterator, timeout=test_constants.SHORT_TIMEOUT
            )
            with self.assertRaises(grpc.RpcError) as exception_context:
                list(responses)
        self.assertIs(
            exception_context.exception.code(),
            grpc.StatusCode.DEADLINE_EXCEEDED,
        )
        service.server.stop(None)

    def testFullDuplexCallCancelled(self):
        service = _CreateService()
        request_iterator = _full_duplex_request_iterator()
        responses = service.stub.FullDuplexCall(request_iterator)
        next(responses)
        responses.cancel()
        with self.assertRaises(grpc.RpcError) as exception_context:
            next(responses)
        self.assertIs(
            exception_context.exception.code(), grpc.StatusCode.CANCELLED
        )
        service.server.stop(None)

    def testFullDuplexCallFailed(self):
        request_iterator = _full_duplex_request_iterator()
        service = _CreateService()
        with service.servicer_methods.fail():
            responses = service.stub.FullDuplexCall(request_iterator)
            with self.assertRaises(grpc.RpcError) as exception_context:
                next(responses)
        self.assertIs(
            exception_context.exception.code(), grpc.StatusCode.UNKNOWN
        )
        service.server.stop(None)

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
            half_duplex_request_iterator(), "not a real RpcContext!"
        )
        for expected_response, response in itertools.zip_longest(
            expected_responses, responses
        ):
            self.assertEqual(expected_response, response)
        service.server.stop(None)

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
                timeout=test_constants.SHORT_TIMEOUT,
            )
            # half-duplex waits for the client to send all info
            with self.assertRaises(grpc.RpcError) as exception_context:
                next(responses)
        self.assertIs(
            exception_context.exception.code(),
            grpc.StatusCode.DEADLINE_EXCEEDED,
        )
        service.server.stop(None)


@unittest.skipIf(
    sys.version_info[0] < 3 or sys.version_info[1] < 6,
    "Unsupported on Python 2.",
)
class SimpleStubsPluginTest(unittest.TestCase):
    servicer_methods = _ServicerMethods()

    class Servicer(service_pb2_grpc.TestServiceServicer):
        def UnaryCall(self, request, context):
            return SimpleStubsPluginTest.servicer_methods.UnaryCall(
                request, context
            )

        def StreamingOutputCall(self, request, context):
            return SimpleStubsPluginTest.servicer_methods.StreamingOutputCall(
                request, context
            )

        def StreamingInputCall(self, request_iterator, context):
            return SimpleStubsPluginTest.servicer_methods.StreamingInputCall(
                request_iterator, context
            )

        def FullDuplexCall(self, request_iterator, context):
            return SimpleStubsPluginTest.servicer_methods.FullDuplexCall(
                request_iterator, context
            )

        def HalfDuplexCall(self, request_iterator, context):
            return SimpleStubsPluginTest.servicer_methods.HalfDuplexCall(
                request_iterator, context
            )

    def setUp(self):
        super(SimpleStubsPluginTest, self).setUp()
        self._server = test_common.test_server()
        service_pb2_grpc.add_TestServiceServicer_to_server(
            self.Servicer(), self._server
        )
        self._port = self._server.add_insecure_port("[::]:0")
        self._server.start()
        self._target = f"localhost:{self._port}"

    def tearDown(self):
        self._server.stop(None)
        super(SimpleStubsPluginTest, self).tearDown()

    def testUnaryCall(self):
        request = request_pb2.SimpleRequest(response_size=13)
        response = service_pb2_grpc.TestService.UnaryCall(
            request,
            self._target,
            channel_credentials=grpc.experimental.insecure_channel_credentials(),
            wait_for_ready=True,
        )
        expected_response = self.servicer_methods.UnaryCall(
            request, "not a real context!"
        )
        self.assertEqual(expected_response, response)

    def testUnaryCallInsecureSugar(self):
        request = request_pb2.SimpleRequest(response_size=13)
        response = service_pb2_grpc.TestService.UnaryCall(
            request, self._target, insecure=True, wait_for_ready=True
        )
        expected_response = self.servicer_methods.UnaryCall(
            request, "not a real context!"
        )
        self.assertEqual(expected_response, response)

    def testStreamingOutputCall(self):
        request = _streaming_output_request()
        expected_responses = self.servicer_methods.StreamingOutputCall(
            request, "not a real RpcContext!"
        )
        responses = service_pb2_grpc.TestService.StreamingOutputCall(
            request,
            self._target,
            channel_credentials=grpc.experimental.insecure_channel_credentials(),
            wait_for_ready=True,
        )
        for expected_response, response in itertools.zip_longest(
            expected_responses, responses
        ):
            self.assertEqual(expected_response, response)

    def testStreamingInputCall(self):
        response = service_pb2_grpc.TestService.StreamingInputCall(
            _streaming_input_request_iterator(),
            self._target,
            channel_credentials=grpc.experimental.insecure_channel_credentials(),
            wait_for_ready=True,
        )
        expected_response = self.servicer_methods.StreamingInputCall(
            _streaming_input_request_iterator(), "not a real RpcContext!"
        )
        self.assertEqual(expected_response, response)

    def testFullDuplexCall(self):
        responses = service_pb2_grpc.TestService.FullDuplexCall(
            _full_duplex_request_iterator(),
            self._target,
            channel_credentials=grpc.experimental.insecure_channel_credentials(),
            wait_for_ready=True,
        )
        expected_responses = self.servicer_methods.FullDuplexCall(
            _full_duplex_request_iterator(), "not a real RpcContext!"
        )
        for expected_response, response in itertools.zip_longest(
            expected_responses, responses
        ):
            self.assertEqual(expected_response, response)

    def testHalfDuplexCall(self):
        def half_duplex_request_iterator():
            request = request_pb2.StreamingOutputCallRequest()
            request.response_parameters.add(size=1, interval_us=0)
            yield request
            request = request_pb2.StreamingOutputCallRequest()
            request.response_parameters.add(size=2, interval_us=0)
            request.response_parameters.add(size=3, interval_us=0)
            yield request

        responses = service_pb2_grpc.TestService.HalfDuplexCall(
            half_duplex_request_iterator(),
            self._target,
            channel_credentials=grpc.experimental.insecure_channel_credentials(),
            wait_for_ready=True,
        )
        expected_responses = self.servicer_methods.HalfDuplexCall(
            half_duplex_request_iterator(), "not a real RpcContext!"
        )
        for expected_response, response in itertools.zip_longest(
            expected_responses, responses
        ):
            self.assertEqual(expected_response, response)


class ModuleMainTest(unittest.TestCase):
    """Test case for running `python -m grpc_tools.protoc`."""

    def test_clean_output(self):
        if sys.executable is None:
            raise unittest.SkipTest(
                "Running on a interpreter that cannot be invoked from the CLI."
            )
        proto_dir_path = os.path.join("src", "proto")
        test_proto_path = os.path.join(
            proto_dir_path, "grpc", "testing", "empty.proto"
        )
        streams = tuple(tempfile.TemporaryFile() for _ in range(2))
        work_dir = tempfile.mkdtemp()
        try:
            invocation = (
                sys.executable,
                "-m",
                "grpc_tools.protoc",
                "--proto_path",
                proto_dir_path,
                "--python_out",
                work_dir,
                "--grpc_python_out",
                work_dir,
                test_proto_path,
            )
            proc = subprocess.Popen(
                invocation, stdout=streams[0], stderr=streams[1]
            )
            proc.wait()
            outs = []
            for stream in streams:
                stream.seek(0)
                self.assertEqual(0, len(stream.read()))
            self.assertEqual(0, proc.returncode)
        except Exception:  # pylint: disable=broad-except
            shutil.rmtree(work_dir)


if __name__ == "__main__":
    unittest.main(verbosity=2)
