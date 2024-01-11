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

import contextlib
import importlib
import itertools
import os
from os import path
import pkgutil
import shutil
import sys
import tempfile
import threading
import unittest

from grpc.beta import implementations
from grpc.beta import interfaces
from grpc.framework.foundation import future
from grpc.framework.interfaces.face import face
from grpc_tools import protoc

from tests.unit.framework.common import test_constants

_RELATIVE_PROTO_PATH = "relative_proto_path"
_RELATIVE_PYTHON_OUT = "relative_python_out"

_PROTO_FILES_PATH_COMPONENTS = (
    (
        "beta_grpc_plugin_test",
        "payload",
        "test_payload.proto",
    ),
    (
        "beta_grpc_plugin_test",
        "requests",
        "r",
        "test_requests.proto",
    ),
    (
        "beta_grpc_plugin_test",
        "responses",
        "test_responses.proto",
    ),
    (
        "beta_grpc_plugin_test",
        "service",
        "test_service.proto",
    ),
)

_PAYLOAD_PB2 = "beta_grpc_plugin_test.payload.test_payload_pb2"
_REQUESTS_PB2 = "beta_grpc_plugin_test.requests.r.test_requests_pb2"
_RESPONSES_PB2 = "beta_grpc_plugin_test.responses.test_responses_pb2"
_SERVICE_PB2 = "beta_grpc_plugin_test.service.test_service_pb2"

# Identifiers of entities we expect to find in the generated module.
SERVICER_IDENTIFIER = "BetaTestServiceServicer"
STUB_IDENTIFIER = "BetaTestServiceStub"
SERVER_FACTORY_IDENTIFIER = "beta_create_TestService_server"
STUB_FACTORY_IDENTIFIER = "beta_create_TestService_stub"


@contextlib.contextmanager
def _system_path(path_insertion):
    old_system_path = sys.path[:]
    sys.path = sys.path[0:1] + path_insertion + sys.path[1:]
    yield
    sys.path = old_system_path


def _create_directory_tree(root, path_components_sequence):
    created = set()
    for path_components in path_components_sequence:
        thus_far = ""
        for path_component in path_components:
            relative_path = path.join(thus_far, path_component)
            if relative_path not in created:
                os.makedirs(path.join(root, relative_path))
                created.add(relative_path)
            thus_far = path.join(thus_far, path_component)


def _massage_proto_content(raw_proto_content):
    imports_substituted = raw_proto_content.replace(
        b'import "tests/protoc_plugin/protos/',
        b'import "beta_grpc_plugin_test/',
    )
    package_statement_substituted = imports_substituted.replace(
        b"package grpc_protoc_plugin;", b"package beta_grpc_protoc_plugin;"
    )
    return package_statement_substituted


def _packagify(directory):
    for subdirectory, _, _ in os.walk(directory):
        init_file_name = path.join(subdirectory, "__init__.py")
        with open(init_file_name, "wb") as init_file:
            init_file.write(b"")


class _ServicerMethods(object):
    def __init__(self, payload_pb2, responses_pb2):
        self._condition = threading.Condition()
        self._paused = False
        self._fail = False
        self._payload_pb2 = payload_pb2
        self._responses_pb2 = responses_pb2

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
        response = self._responses_pb2.SimpleResponse()
        response.payload.payload_type = self._payload_pb2.COMPRESSABLE
        response.payload.payload_compressable = "a" * request.response_size
        self._control()
        return response

    def StreamingOutputCall(self, request, unused_rpc_context):
        for parameter in request.response_parameters:
            response = self._responses_pb2.StreamingOutputCallResponse()
            response.payload.payload_type = self._payload_pb2.COMPRESSABLE
            response.payload.payload_compressable = "a" * parameter.size
            self._control()
            yield response

    def StreamingInputCall(self, request_iter, unused_rpc_context):
        response = self._responses_pb2.StreamingInputCallResponse()
        aggregated_payload_size = 0
        for request in request_iter:
            aggregated_payload_size += len(request.payload.payload_compressable)
        response.aggregated_payload_size = aggregated_payload_size
        self._control()
        return response

    def FullDuplexCall(self, request_iter, unused_rpc_context):
        for request in request_iter:
            for parameter in request.response_parameters:
                response = self._responses_pb2.StreamingOutputCallResponse()
                response.payload.payload_type = self._payload_pb2.COMPRESSABLE
                response.payload.payload_compressable = "a" * parameter.size
                self._control()
                yield response

    def HalfDuplexCall(self, request_iter, unused_rpc_context):
        responses = []
        for request in request_iter:
            for parameter in request.response_parameters:
                response = self._responses_pb2.StreamingOutputCallResponse()
                response.payload.payload_type = self._payload_pb2.COMPRESSABLE
                response.payload.payload_compressable = "a" * parameter.size
                self._control()
                responses.append(response)
        for response in responses:
            yield response


@contextlib.contextmanager
def _CreateService(payload_pb2, responses_pb2, service_pb2):
    """Provides a servicer backend and a stub.

    The servicer is just the implementation of the actual servicer passed to the
    face player of the python RPC implementation; the two are detached.

    Yields:
      A (servicer_methods, stub) pair where servicer_methods is the back-end of
        the service bound to the stub and stub is the stub on which to invoke
        RPCs.
    """
    servicer_methods = _ServicerMethods(payload_pb2, responses_pb2)

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
    port = server.add_insecure_port("[::]:0")
    server.start()
    channel = implementations.insecure_channel("localhost", port)
    stub = getattr(service_pb2, STUB_FACTORY_IDENTIFIER)(channel)
    yield servicer_methods, stub
    server.stop(0)


@contextlib.contextmanager
def _CreateIncompleteService(service_pb2):
    """Provides a servicer backend that fails to implement methods and its stub.

    The servicer is just the implementation of the actual servicer passed to the
    face player of the python RPC implementation; the two are detached.
    Args:
      service_pb2: The service_pb2 module generated by this test.
    Yields:
      A (servicer_methods, stub) pair where servicer_methods is the back-end of
        the service bound to the stub and stub is the stub on which to invoke
        RPCs.
    """

    class Servicer(getattr(service_pb2, SERVICER_IDENTIFIER)):
        pass

    servicer = Servicer()
    server = getattr(service_pb2, SERVER_FACTORY_IDENTIFIER)(servicer)
    port = server.add_insecure_port("[::]:0")
    server.start()
    channel = implementations.insecure_channel("localhost", port)
    stub = getattr(service_pb2, STUB_FACTORY_IDENTIFIER)(channel)
    yield None, stub
    server.stop(0)


def _streaming_input_request_iterator(payload_pb2, requests_pb2):
    for _ in range(3):
        request = requests_pb2.StreamingInputCallRequest()
        request.payload.payload_type = payload_pb2.COMPRESSABLE
        request.payload.payload_compressable = "a"
        yield request


def _streaming_output_request(requests_pb2):
    request = requests_pb2.StreamingOutputCallRequest()
    sizes = [1, 2, 3]
    request.response_parameters.add(size=sizes[0], interval_us=0)
    request.response_parameters.add(size=sizes[1], interval_us=0)
    request.response_parameters.add(size=sizes[2], interval_us=0)
    return request


def _full_duplex_request_iterator(requests_pb2):
    request = requests_pb2.StreamingOutputCallRequest()
    request.response_parameters.add(size=1, interval_us=0)
    yield request
    request = requests_pb2.StreamingOutputCallRequest()
    request.response_parameters.add(size=2, interval_us=0)
    request.response_parameters.add(size=3, interval_us=0)
    yield request


class PythonPluginTest(unittest.TestCase):
    """Test case for the gRPC Python protoc-plugin.

    While reading these tests, remember that the futures API
    (`stub.method.future()`) only gives futures for the *response-unary*
    methods and does not exist for response-streaming methods.
    """

    def setUp(self):
        self._directory = tempfile.mkdtemp(dir=".")
        self._proto_path = path.join(self._directory, _RELATIVE_PROTO_PATH)
        self._python_out = path.join(self._directory, _RELATIVE_PYTHON_OUT)

        os.makedirs(self._proto_path)
        os.makedirs(self._python_out)

        directories_path_components = {
            proto_file_path_components[:-1]
            for proto_file_path_components in _PROTO_FILES_PATH_COMPONENTS
        }
        _create_directory_tree(self._proto_path, directories_path_components)
        self._proto_file_names = set()
        for proto_file_path_components in _PROTO_FILES_PATH_COMPONENTS:
            raw_proto_content = pkgutil.get_data(
                "tests.protoc_plugin.protos",
                path.join(*proto_file_path_components[1:]),
            )
            massaged_proto_content = _massage_proto_content(raw_proto_content)
            proto_file_name = path.join(
                self._proto_path, *proto_file_path_components
            )
            with open(proto_file_name, "wb") as proto_file:
                proto_file.write(massaged_proto_content)
            self._proto_file_names.add(proto_file_name)

    def tearDown(self):
        shutil.rmtree(self._directory)

    def _protoc(self):
        args = [
            "",
            "--proto_path={}".format(self._proto_path),
            "--python_out={}".format(self._python_out),
            "--grpc_python_out=grpc_1_0:{}".format(self._python_out),
        ] + list(self._proto_file_names)
        protoc_exit_code = protoc.main(args)
        self.assertEqual(0, protoc_exit_code)

        _packagify(self._python_out)

        with _system_path([self._python_out]):
            self._payload_pb2 = importlib.import_module(_PAYLOAD_PB2)
            self._requests_pb2 = importlib.import_module(_REQUESTS_PB2)
            self._responses_pb2 = importlib.import_module(_RESPONSES_PB2)
            self._service_pb2 = importlib.import_module(_SERVICE_PB2)

    def testImportAttributes(self):
        self._protoc()

        # check that we can access the generated module and its members.
        self.assertIsNotNone(
            getattr(self._service_pb2, SERVICER_IDENTIFIER, None)
        )
        self.assertIsNotNone(getattr(self._service_pb2, STUB_IDENTIFIER, None))
        self.assertIsNotNone(
            getattr(self._service_pb2, SERVER_FACTORY_IDENTIFIER, None)
        )
        self.assertIsNotNone(
            getattr(self._service_pb2, STUB_FACTORY_IDENTIFIER, None)
        )

    def testUpDown(self):
        self._protoc()

        with _CreateService(
            self._payload_pb2, self._responses_pb2, self._service_pb2
        ):
            self._requests_pb2.SimpleRequest(response_size=13)

    def testIncompleteServicer(self):
        self._protoc()

        with _CreateIncompleteService(self._service_pb2) as (_, stub):
            request = self._requests_pb2.SimpleRequest(response_size=13)
            try:
                stub.UnaryCall(request, test_constants.LONG_TIMEOUT)
            except face.AbortionError as error:
                self.assertEqual(
                    interfaces.StatusCode.UNIMPLEMENTED, error.code
                )

    def testUnaryCall(self):
        self._protoc()

        with _CreateService(
            self._payload_pb2, self._responses_pb2, self._service_pb2
        ) as (methods, stub):
            request = self._requests_pb2.SimpleRequest(response_size=13)
            response = stub.UnaryCall(request, test_constants.LONG_TIMEOUT)
        expected_response = methods.UnaryCall(request, "not a real context!")
        self.assertEqual(expected_response, response)

    def testUnaryCallFuture(self):
        self._protoc()

        with _CreateService(
            self._payload_pb2, self._responses_pb2, self._service_pb2
        ) as (methods, stub):
            request = self._requests_pb2.SimpleRequest(response_size=13)
            # Check that the call does not block waiting for the server to respond.
            with methods.pause():
                response_future = stub.UnaryCall.future(
                    request, test_constants.LONG_TIMEOUT
                )
            response = response_future.result()
        expected_response = methods.UnaryCall(request, "not a real RpcContext!")
        self.assertEqual(expected_response, response)

    def testUnaryCallFutureExpired(self):
        self._protoc()

        with _CreateService(
            self._payload_pb2, self._responses_pb2, self._service_pb2
        ) as (methods, stub):
            request = self._requests_pb2.SimpleRequest(response_size=13)
            with methods.pause():
                response_future = stub.UnaryCall.future(
                    request, test_constants.SHORT_TIMEOUT
                )
                with self.assertRaises(face.ExpirationError):
                    response_future.result()

    def testUnaryCallFutureCancelled(self):
        self._protoc()

        with _CreateService(
            self._payload_pb2, self._responses_pb2, self._service_pb2
        ) as (methods, stub):
            request = self._requests_pb2.SimpleRequest(response_size=13)
            with methods.pause():
                response_future = stub.UnaryCall.future(request, 1)
                response_future.cancel()
                self.assertTrue(response_future.cancelled())

    def testUnaryCallFutureFailed(self):
        self._protoc()

        with _CreateService(
            self._payload_pb2, self._responses_pb2, self._service_pb2
        ) as (methods, stub):
            request = self._requests_pb2.SimpleRequest(response_size=13)
            with methods.fail():
                response_future = stub.UnaryCall.future(
                    request, test_constants.LONG_TIMEOUT
                )
                self.assertIsNotNone(response_future.exception())

    def testStreamingOutputCall(self):
        self._protoc()

        with _CreateService(
            self._payload_pb2, self._responses_pb2, self._service_pb2
        ) as (methods, stub):
            request = _streaming_output_request(self._requests_pb2)
            responses = stub.StreamingOutputCall(
                request, test_constants.LONG_TIMEOUT
            )
            expected_responses = methods.StreamingOutputCall(
                request, "not a real RpcContext!"
            )
            for expected_response, response in itertools.zip_longest(
                expected_responses, responses
            ):
                self.assertEqual(expected_response, response)

    def testStreamingOutputCallExpired(self):
        self._protoc()

        with _CreateService(
            self._payload_pb2, self._responses_pb2, self._service_pb2
        ) as (methods, stub):
            request = _streaming_output_request(self._requests_pb2)
            with methods.pause():
                responses = stub.StreamingOutputCall(
                    request, test_constants.SHORT_TIMEOUT
                )
                with self.assertRaises(face.ExpirationError):
                    list(responses)

    def testStreamingOutputCallCancelled(self):
        self._protoc()

        with _CreateService(
            self._payload_pb2, self._responses_pb2, self._service_pb2
        ) as (methods, stub):
            request = _streaming_output_request(self._requests_pb2)
            responses = stub.StreamingOutputCall(
                request, test_constants.LONG_TIMEOUT
            )
            next(responses)
            responses.cancel()
            with self.assertRaises(face.CancellationError):
                next(responses)

    def testStreamingOutputCallFailed(self):
        self._protoc()

        with _CreateService(
            self._payload_pb2, self._responses_pb2, self._service_pb2
        ) as (methods, stub):
            request = _streaming_output_request(self._requests_pb2)
            with methods.fail():
                responses = stub.StreamingOutputCall(request, 1)
                self.assertIsNotNone(responses)
                with self.assertRaises(face.RemoteError):
                    next(responses)

    def testStreamingInputCall(self):
        self._protoc()

        with _CreateService(
            self._payload_pb2, self._responses_pb2, self._service_pb2
        ) as (methods, stub):
            response = stub.StreamingInputCall(
                _streaming_input_request_iterator(
                    self._payload_pb2, self._requests_pb2
                ),
                test_constants.LONG_TIMEOUT,
            )
        expected_response = methods.StreamingInputCall(
            _streaming_input_request_iterator(
                self._payload_pb2, self._requests_pb2
            ),
            "not a real RpcContext!",
        )
        self.assertEqual(expected_response, response)

    def testStreamingInputCallFuture(self):
        self._protoc()

        with _CreateService(
            self._payload_pb2, self._responses_pb2, self._service_pb2
        ) as (methods, stub):
            with methods.pause():
                response_future = stub.StreamingInputCall.future(
                    _streaming_input_request_iterator(
                        self._payload_pb2, self._requests_pb2
                    ),
                    test_constants.LONG_TIMEOUT,
                )
            response = response_future.result()
        expected_response = methods.StreamingInputCall(
            _streaming_input_request_iterator(
                self._payload_pb2, self._requests_pb2
            ),
            "not a real RpcContext!",
        )
        self.assertEqual(expected_response, response)

    def testStreamingInputCallFutureExpired(self):
        self._protoc()

        with _CreateService(
            self._payload_pb2, self._responses_pb2, self._service_pb2
        ) as (methods, stub):
            with methods.pause():
                response_future = stub.StreamingInputCall.future(
                    _streaming_input_request_iterator(
                        self._payload_pb2, self._requests_pb2
                    ),
                    test_constants.SHORT_TIMEOUT,
                )
                with self.assertRaises(face.ExpirationError):
                    response_future.result()
                self.assertIsInstance(
                    response_future.exception(), face.ExpirationError
                )

    def testStreamingInputCallFutureCancelled(self):
        self._protoc()

        with _CreateService(
            self._payload_pb2, self._responses_pb2, self._service_pb2
        ) as (methods, stub):
            with methods.pause():
                response_future = stub.StreamingInputCall.future(
                    _streaming_input_request_iterator(
                        self._payload_pb2, self._requests_pb2
                    ),
                    test_constants.LONG_TIMEOUT,
                )
                response_future.cancel()
                self.assertTrue(response_future.cancelled())
            with self.assertRaises(future.CancelledError):
                response_future.result()

    def testStreamingInputCallFutureFailed(self):
        self._protoc()

        with _CreateService(
            self._payload_pb2, self._responses_pb2, self._service_pb2
        ) as (methods, stub):
            with methods.fail():
                response_future = stub.StreamingInputCall.future(
                    _streaming_input_request_iterator(
                        self._payload_pb2, self._requests_pb2
                    ),
                    test_constants.LONG_TIMEOUT,
                )
                self.assertIsNotNone(response_future.exception())

    def testFullDuplexCall(self):
        self._protoc()

        with _CreateService(
            self._payload_pb2, self._responses_pb2, self._service_pb2
        ) as (methods, stub):
            responses = stub.FullDuplexCall(
                _full_duplex_request_iterator(self._requests_pb2),
                test_constants.LONG_TIMEOUT,
            )
            expected_responses = methods.FullDuplexCall(
                _full_duplex_request_iterator(self._requests_pb2),
                "not a real RpcContext!",
            )
            for expected_response, response in itertools.zip_longest(
                expected_responses, responses
            ):
                self.assertEqual(expected_response, response)

    def testFullDuplexCallExpired(self):
        self._protoc()

        request_iterator = _full_duplex_request_iterator(self._requests_pb2)
        with _CreateService(
            self._payload_pb2, self._responses_pb2, self._service_pb2
        ) as (methods, stub):
            with methods.pause():
                responses = stub.FullDuplexCall(
                    request_iterator, test_constants.SHORT_TIMEOUT
                )
                with self.assertRaises(face.ExpirationError):
                    list(responses)

    def testFullDuplexCallCancelled(self):
        self._protoc()

        with _CreateService(
            self._payload_pb2, self._responses_pb2, self._service_pb2
        ) as (methods, stub):
            request_iterator = _full_duplex_request_iterator(self._requests_pb2)
            responses = stub.FullDuplexCall(
                request_iterator, test_constants.LONG_TIMEOUT
            )
            next(responses)
            responses.cancel()
            with self.assertRaises(face.CancellationError):
                next(responses)

    def testFullDuplexCallFailed(self):
        self._protoc()

        request_iterator = _full_duplex_request_iterator(self._requests_pb2)
        with _CreateService(
            self._payload_pb2, self._responses_pb2, self._service_pb2
        ) as (methods, stub):
            with methods.fail():
                responses = stub.FullDuplexCall(
                    request_iterator, test_constants.LONG_TIMEOUT
                )
                self.assertIsNotNone(responses)
                with self.assertRaises(face.RemoteError):
                    next(responses)

    def testHalfDuplexCall(self):
        self._protoc()

        with _CreateService(
            self._payload_pb2, self._responses_pb2, self._service_pb2
        ) as (methods, stub):

            def half_duplex_request_iterator():
                request = self._requests_pb2.StreamingOutputCallRequest()
                request.response_parameters.add(size=1, interval_us=0)
                yield request
                request = self._requests_pb2.StreamingOutputCallRequest()
                request.response_parameters.add(size=2, interval_us=0)
                request.response_parameters.add(size=3, interval_us=0)
                yield request

            responses = stub.HalfDuplexCall(
                half_duplex_request_iterator(), test_constants.LONG_TIMEOUT
            )
            expected_responses = methods.HalfDuplexCall(
                half_duplex_request_iterator(), "not a real RpcContext!"
            )
            for check in itertools.zip_longest(expected_responses, responses):
                expected_response, response = check
                self.assertEqual(expected_response, response)

    def testHalfDuplexCallWedged(self):
        self._protoc()

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
            request = self._requests_pb2.StreamingOutputCallRequest()
            request.response_parameters.add(size=1, interval_us=0)
            yield request
            with condition:
                while wait_cell[0]:
                    condition.wait()

        with _CreateService(
            self._payload_pb2, self._responses_pb2, self._service_pb2
        ) as (methods, stub):
            with wait():
                responses = stub.HalfDuplexCall(
                    half_duplex_request_iterator(), test_constants.SHORT_TIMEOUT
                )
                # half-duplex waits for the client to send all info
                with self.assertRaises(face.ExpirationError):
                    next(responses)


if __name__ == "__main__":
    unittest.main(verbosity=2)
