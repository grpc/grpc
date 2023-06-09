# Copyright 2017 gRPC authors.
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

import time
import unittest

import grpc
import grpc_testing

from tests.testing import _application_common
from tests.testing import _application_testing_common
from tests.testing import _server_application
from tests.testing.proto import services_pb2


class FirstServiceServicerTest(unittest.TestCase):
    def setUp(self):
        self._real_time = grpc_testing.strict_real_time()
        self._fake_time = grpc_testing.strict_fake_time(time.time())
        servicer = _server_application.FirstServiceServicer()
        descriptors_to_servicers = {
            _application_testing_common.FIRST_SERVICE: servicer
        }
        self._real_time_server = grpc_testing.server_from_dictionary(
            descriptors_to_servicers, self._real_time
        )
        self._fake_time_server = grpc_testing.server_from_dictionary(
            descriptors_to_servicers, self._fake_time
        )

    def test_successful_unary_unary(self):
        rpc = self._real_time_server.invoke_unary_unary(
            _application_testing_common.FIRST_SERVICE_UNUN,
            (),
            _application_common.UNARY_UNARY_REQUEST,
            None,
        )
        initial_metadata = rpc.initial_metadata()
        response, trailing_metadata, code, details = rpc.termination()

        self.assertEqual(_application_common.UNARY_UNARY_RESPONSE, response)
        self.assertIs(code, grpc.StatusCode.OK)

    def test_successful_unary_stream(self):
        rpc = self._real_time_server.invoke_unary_stream(
            _application_testing_common.FIRST_SERVICE_UNSTRE,
            (),
            _application_common.UNARY_STREAM_REQUEST,
            None,
        )
        initial_metadata = rpc.initial_metadata()
        trailing_metadata, code, details = rpc.termination()

        self.assertIs(code, grpc.StatusCode.OK)

    def test_successful_stream_unary(self):
        rpc = self._real_time_server.invoke_stream_unary(
            _application_testing_common.FIRST_SERVICE_STREUN, (), None
        )
        rpc.send_request(_application_common.STREAM_UNARY_REQUEST)
        rpc.send_request(_application_common.STREAM_UNARY_REQUEST)
        rpc.send_request(_application_common.STREAM_UNARY_REQUEST)
        rpc.requests_closed()
        initial_metadata = rpc.initial_metadata()
        response, trailing_metadata, code, details = rpc.termination()

        self.assertEqual(_application_common.STREAM_UNARY_RESPONSE, response)
        self.assertIs(code, grpc.StatusCode.OK)

    def test_successful_stream_stream(self):
        rpc = self._real_time_server.invoke_stream_stream(
            _application_testing_common.FIRST_SERVICE_STRESTRE, (), None
        )
        rpc.send_request(_application_common.STREAM_STREAM_REQUEST)
        initial_metadata = rpc.initial_metadata()
        responses = [
            rpc.take_response(),
            rpc.take_response(),
        ]
        rpc.send_request(_application_common.STREAM_STREAM_REQUEST)
        rpc.send_request(_application_common.STREAM_STREAM_REQUEST)
        responses.extend(
            [
                rpc.take_response(),
                rpc.take_response(),
                rpc.take_response(),
                rpc.take_response(),
            ]
        )
        rpc.requests_closed()
        trailing_metadata, code, details = rpc.termination()

        for response in responses:
            self.assertEqual(
                _application_common.STREAM_STREAM_RESPONSE, response
            )
        self.assertIs(code, grpc.StatusCode.OK)

    def test_mutating_stream_stream(self):
        rpc = self._real_time_server.invoke_stream_stream(
            _application_testing_common.FIRST_SERVICE_STRESTRE, (), None
        )
        rpc.send_request(_application_common.STREAM_STREAM_MUTATING_REQUEST)
        initial_metadata = rpc.initial_metadata()
        responses = [
            rpc.take_response()
            for _ in range(_application_common.STREAM_STREAM_MUTATING_COUNT)
        ]
        rpc.send_request(_application_common.STREAM_STREAM_MUTATING_REQUEST)
        responses.extend(
            [
                rpc.take_response()
                for _ in range(_application_common.STREAM_STREAM_MUTATING_COUNT)
            ]
        )
        rpc.requests_closed()
        _, _, _ = rpc.termination()
        expected_responses = (
            services_pb2.Bottom(first_bottom_field=0),
            services_pb2.Bottom(first_bottom_field=1),
            services_pb2.Bottom(first_bottom_field=0),
            services_pb2.Bottom(first_bottom_field=1),
        )
        self.assertSequenceEqual(expected_responses, responses)

    def test_server_rpc_idempotence(self):
        rpc = self._real_time_server.invoke_unary_unary(
            _application_testing_common.FIRST_SERVICE_UNUN,
            (),
            _application_common.UNARY_UNARY_REQUEST,
            None,
        )
        first_initial_metadata = rpc.initial_metadata()
        second_initial_metadata = rpc.initial_metadata()
        third_initial_metadata = rpc.initial_metadata()
        first_termination = rpc.termination()
        second_termination = rpc.termination()
        third_termination = rpc.termination()

        for later_initial_metadata in (
            second_initial_metadata,
            third_initial_metadata,
        ):
            self.assertEqual(first_initial_metadata, later_initial_metadata)
        response = first_termination[0]
        terminal_metadata = first_termination[1]
        code = first_termination[2]
        details = first_termination[3]
        for later_termination in (
            second_termination,
            third_termination,
        ):
            self.assertEqual(response, later_termination[0])
            self.assertEqual(terminal_metadata, later_termination[1])
            self.assertIs(code, later_termination[2])
            self.assertEqual(details, later_termination[3])
        self.assertEqual(_application_common.UNARY_UNARY_RESPONSE, response)
        self.assertIs(code, grpc.StatusCode.OK)

    def test_misbehaving_client_unary_unary(self):
        rpc = self._real_time_server.invoke_unary_unary(
            _application_testing_common.FIRST_SERVICE_UNUN,
            (),
            _application_common.ERRONEOUS_UNARY_UNARY_REQUEST,
            None,
        )
        initial_metadata = rpc.initial_metadata()
        response, trailing_metadata, code, details = rpc.termination()

        self.assertIsNot(code, grpc.StatusCode.OK)

    def test_infinite_request_stream_real_time(self):
        rpc = self._real_time_server.invoke_stream_unary(
            _application_testing_common.FIRST_SERVICE_STREUN,
            (),
            _application_common.INFINITE_REQUEST_STREAM_TIMEOUT,
        )
        rpc.send_request(_application_common.STREAM_UNARY_REQUEST)
        rpc.send_request(_application_common.STREAM_UNARY_REQUEST)
        rpc.send_request(_application_common.STREAM_UNARY_REQUEST)
        initial_metadata = rpc.initial_metadata()
        self._real_time.sleep_for(
            _application_common.INFINITE_REQUEST_STREAM_TIMEOUT * 2
        )
        rpc.send_request(_application_common.STREAM_UNARY_REQUEST)
        response, trailing_metadata, code, details = rpc.termination()

        self.assertIs(code, grpc.StatusCode.DEADLINE_EXCEEDED)

    def test_infinite_request_stream_fake_time(self):
        rpc = self._fake_time_server.invoke_stream_unary(
            _application_testing_common.FIRST_SERVICE_STREUN,
            (),
            _application_common.INFINITE_REQUEST_STREAM_TIMEOUT,
        )
        rpc.send_request(_application_common.STREAM_UNARY_REQUEST)
        rpc.send_request(_application_common.STREAM_UNARY_REQUEST)
        rpc.send_request(_application_common.STREAM_UNARY_REQUEST)
        initial_metadata = rpc.initial_metadata()
        self._fake_time.sleep_for(
            _application_common.INFINITE_REQUEST_STREAM_TIMEOUT * 2
        )
        rpc.send_request(_application_common.STREAM_UNARY_REQUEST)
        response, trailing_metadata, code, details = rpc.termination()

        self.assertIs(code, grpc.StatusCode.DEADLINE_EXCEEDED)

    def test_servicer_context_abort(self):
        rpc = self._real_time_server.invoke_unary_unary(
            _application_testing_common.FIRST_SERVICE_UNUN,
            (),
            _application_common.ABORT_REQUEST,
            None,
        )
        _, _, code, _ = rpc.termination()
        self.assertIs(code, grpc.StatusCode.PERMISSION_DENIED)
        rpc = self._real_time_server.invoke_unary_unary(
            _application_testing_common.FIRST_SERVICE_UNUN,
            (),
            _application_common.ABORT_SUCCESS_QUERY,
            None,
        )
        response, _, code, _ = rpc.termination()
        self.assertEqual(_application_common.ABORT_SUCCESS_RESPONSE, response)
        self.assertIs(code, grpc.StatusCode.OK)


if __name__ == "__main__":
    unittest.main(verbosity=2)
