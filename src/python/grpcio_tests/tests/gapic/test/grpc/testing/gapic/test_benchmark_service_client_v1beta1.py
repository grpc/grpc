# Copyright 2017, Google Inc. All rights reserved.
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

"""Unit tests."""

import mock
import unittest

from google.gax import errors

from grpc.testing import messages_pb2
from grpc.testing.gapic import benchmark_service_client


class CustomException(Exception):
    pass


class TestBenchmarkServiceClient(unittest.TestCase):

    @mock.patch('google.gax.config.create_stub', spec=True)
    def test_unary_call(self, mock_create_stub):
        # Mock gRPC layer
        grpc_stub = mock.Mock()
        mock_create_stub.return_value = grpc_stub

        client = benchmark_service_client.BenchmarkServiceClient()

        # Mock response
        username = 'username-265713450'
        oauth_scope = 'oauthScope443818668'
        expected_response = messages_pb2.SimpleResponse(username=username, oauth_scope=oauth_scope)
        grpc_stub.UnaryCall.return_value = expected_response

        response = client.unary_call()
        self.assertEqual(expected_response, response)

        grpc_stub.UnaryCall.assert_called_once()
        args, kwargs = grpc_stub.UnaryCall.call_args
        self.assertEqual(len(args), 2)
        self.assertEqual(len(kwargs), 1)
        self.assertIn('metadata', kwargs)
        actual_request = args[0]

        expected_request = messages_pb2.SimpleRequest()
        self.assertEqual(expected_request, actual_request)

    @mock.patch('google.gax.config.API_ERRORS', (CustomException,))
    @mock.patch('google.gax.config.create_stub', spec=True)
    def test_unary_call_exception(self, mock_create_stub):
        # Mock gRPC layer
        grpc_stub = mock.Mock()
        mock_create_stub.return_value = grpc_stub

        client = benchmark_service_client.BenchmarkServiceClient()

        # Mock exception response
        grpc_stub.UnaryCall.side_effect = CustomException()

        self.assertRaises(errors.GaxError, client.unary_call)

    @mock.patch('google.gax.config.create_stub', spec=True)
    def test_streaming_call(self, mock_create_stub):
        # Mock gRPC layer
        grpc_stub = mock.Mock()
        mock_create_stub.return_value = grpc_stub

        client = benchmark_service_client.BenchmarkServiceClient()

        # Mock request
        request = messages_pb2.SimpleRequest()
        requests = [request]

        # Mock response
        username = 'username-265713450'
        oauth_scope = 'oauthScope443818668'
        expected_response = messages_pb2.SimpleResponse(username=username, oauth_scope=oauth_scope)
        grpc_stub.StreamingCall.return_value = iter([expected_response])

        response = client.streaming_call(requests)
        resources = list(response)
        self.assertEqual(1, len(resources))
        self.assertEqual(expected_response, resources[0])

        grpc_stub.StreamingCall.assert_called_once()
        args, kwargs = grpc_stub.StreamingCall.call_args
        self.assertEqual(len(args), 2)
        self.assertEqual(len(kwargs), 1)
        self.assertIn('metadata', kwargs)
        actual_requests = args[0]
        self.assertEqual(1, len(actual_requests))
        actual_request = list(actual_requests)[0]
        self.assertEqual(request, actual_request)

    @mock.patch('google.gax.config.API_ERRORS', (CustomException,))
    @mock.patch('google.gax.config.create_stub', spec=True)
    def test_streaming_call_exception(self, mock_create_stub):
        # Mock gRPC layer
        grpc_stub = mock.Mock()
        mock_create_stub.return_value = grpc_stub

        client = benchmark_service_client.BenchmarkServiceClient()

        # Mock request
        request = messages_pb2.SimpleRequest()
        requests = [request]

        # Mock exception response
        grpc_stub.StreamingCall.side_effect = CustomException()

        self.assertRaises(errors.GaxError, client.streaming_call, requests)

    @mock.patch('google.gax.config.create_stub', spec=True)
    def test_streaming_from_client(self, mock_create_stub):
        # Mock gRPC layer
        grpc_stub = mock.Mock()
        mock_create_stub.return_value = grpc_stub

        client = benchmark_service_client.BenchmarkServiceClient()

        # Mock request
        request = messages_pb2.SimpleRequest()
        requests = [request]

        # Mock response
        username = 'username-265713450'
        oauth_scope = 'oauthScope443818668'
        expected_response = messages_pb2.SimpleResponse(username=username, oauth_scope=oauth_scope)
        grpc_stub.StreamingFromClient.return_value = expected_response

        response = client.streaming_from_client(requests)
        self.assertEqual(expected_response, response)

        grpc_stub.StreamingFromClient.assert_called_once()
        args, kwargs = grpc_stub.StreamingFromClient.call_args
        self.assertEqual(len(args), 2)
        self.assertEqual(len(kwargs), 1)
        self.assertIn('metadata', kwargs)
        actual_requests = args[0]
        self.assertEqual(1, len(actual_requests))
        actual_request = list(actual_requests)[0]
        self.assertEqual(request, actual_request)

    @mock.patch('google.gax.config.API_ERRORS', (CustomException,))
    @mock.patch('google.gax.config.create_stub', spec=True)
    def test_streaming_from_client_exception(self, mock_create_stub):
        # Mock gRPC layer
        grpc_stub = mock.Mock()
        mock_create_stub.return_value = grpc_stub

        client = benchmark_service_client.BenchmarkServiceClient()

        # Mock request
        request = messages_pb2.SimpleRequest()
        requests = [request]

        # Mock exception response
        grpc_stub.StreamingFromClient.side_effect = CustomException()

        self.assertRaises(errors.GaxError, client.streaming_from_client, requests)

    @mock.patch('google.gax.config.create_stub', spec=True)
    def test_streaming_from_server(self, mock_create_stub):
        # Mock gRPC layer
        grpc_stub = mock.Mock()
        mock_create_stub.return_value = grpc_stub

        client = benchmark_service_client.BenchmarkServiceClient()

        # Mock response
        username = 'username-265713450'
        oauth_scope = 'oauthScope443818668'
        expected_response = messages_pb2.SimpleResponse(username=username, oauth_scope=oauth_scope)
        grpc_stub.StreamingFromServer.return_value = iter([expected_response])

        response = client.streaming_from_server()
        resources = list(response)
        self.assertEqual(1, len(resources))
        self.assertEqual(expected_response, resources[0])

        grpc_stub.StreamingFromServer.assert_called_once()
        args, kwargs = grpc_stub.StreamingFromServer.call_args
        self.assertEqual(len(args), 2)
        self.assertEqual(len(kwargs), 1)
        self.assertIn('metadata', kwargs)
        actual_request = args[0]

        expected_request = messages_pb2.SimpleRequest()
        self.assertEqual(expected_request, actual_request)

    @mock.patch('google.gax.config.API_ERRORS', (CustomException,))
    @mock.patch('google.gax.config.create_stub', spec=True)
    def test_streaming_from_server_exception(self, mock_create_stub):
        # Mock gRPC layer
        grpc_stub = mock.Mock()
        mock_create_stub.return_value = grpc_stub

        client = benchmark_service_client.BenchmarkServiceClient()

        # Mock exception response
        grpc_stub.StreamingFromServer.side_effect = CustomException()

        self.assertRaises(errors.GaxError, client.streaming_from_server)

    @mock.patch('google.gax.config.create_stub', spec=True)
    def test_streaming_both_ways(self, mock_create_stub):
        # Mock gRPC layer
        grpc_stub = mock.Mock()
        mock_create_stub.return_value = grpc_stub

        client = benchmark_service_client.BenchmarkServiceClient()

        # Mock request
        request = messages_pb2.SimpleRequest()
        requests = [request]

        # Mock response
        username = 'username-265713450'
        oauth_scope = 'oauthScope443818668'
        expected_response = messages_pb2.SimpleResponse(username=username, oauth_scope=oauth_scope)
        grpc_stub.StreamingBothWays.return_value = iter([expected_response])

        response = client.streaming_both_ways(requests)
        resources = list(response)
        self.assertEqual(1, len(resources))
        self.assertEqual(expected_response, resources[0])

        grpc_stub.StreamingBothWays.assert_called_once()
        args, kwargs = grpc_stub.StreamingBothWays.call_args
        self.assertEqual(len(args), 2)
        self.assertEqual(len(kwargs), 1)
        self.assertIn('metadata', kwargs)
        actual_requests = args[0]
        self.assertEqual(1, len(actual_requests))
        actual_request = list(actual_requests)[0]
        self.assertEqual(request, actual_request)

    @mock.patch('google.gax.config.API_ERRORS', (CustomException,))
    @mock.patch('google.gax.config.create_stub', spec=True)
    def test_streaming_both_ways_exception(self, mock_create_stub):
        # Mock gRPC layer
        grpc_stub = mock.Mock()
        mock_create_stub.return_value = grpc_stub

        client = benchmark_service_client.BenchmarkServiceClient()

        # Mock request
        request = messages_pb2.SimpleRequest()
        requests = [request]

        # Mock exception response
        grpc_stub.StreamingBothWays.side_effect = CustomException()

        self.assertRaises(errors.GaxError, client.streaming_both_ways, requests)
