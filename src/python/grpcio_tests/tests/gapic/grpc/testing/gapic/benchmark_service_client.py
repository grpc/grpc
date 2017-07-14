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
#
# EDITING INSTRUCTIONS
# This file was generated from the file
# https://github.com/google/googleapis/blob/master/src/proto/grpc/testing/services.proto,
# and updates to that file get reflected here through a refresh process.
# For the short term, the refresh process will only be runnable by Google engineers.
#
# The only allowed edits are to method and file documentation. A 3-way
# merge preserves those additions if the generated source changes.

"""Accesses the grpc.testing BenchmarkService API."""

import collections
import json
import os
import pkg_resources
import platform

from google.gax import api_callable
from google.gax import config
from google.gax import path_template
import google.gax

from src.proto.grpc.testing import messages_pb2
from src.proto.grpc.testing import services_pb2


class BenchmarkServiceClient(object):
    SERVICE_ADDRESS = 'testing.googleapis.com'
    """The default address of the service."""

    DEFAULT_SERVICE_PORT = 443
    """The default port of the service."""

    # The scopes needed to make gRPC calls to all of the methods defined in
    # this service
    _ALL_SCOPES = (
        'https://www.googleapis.com/auth/cloud-platform',
        'https://www.googleapis.com/auth/pubsub',
    )

    def __init__(self,
            service_path=SERVICE_ADDRESS,
            port=DEFAULT_SERVICE_PORT,
            channel=None,
            credentials=None,
            ssl_credentials=None,
            scopes=None,
            client_config=None,
            app_name=None,
            app_version='',
            lib_name=None,
            lib_version='',
            metrics_headers=()):
        """Constructor.

        Args:
          service_path (string): The domain name of the API remote host.
          port (int): The port on which to connect to the remote host.
          channel (:class:`grpc.Channel`): A ``Channel`` instance through
            which to make calls.
          credentials (object): The authorization credentials to attach to
            requests. These credentials identify this application to the
            service.
          ssl_credentials (:class:`grpc.ChannelCredentials`): A
            ``ChannelCredentials`` instance for use with an SSL-enabled
            channel.
          scopes (list[string]): A list of OAuth2 scopes to attach to requests.
          client_config (dict):
            A dictionary for call options for each method. See
            :func:`google.gax.construct_settings` for the structure of
            this data. Falls back to the default config if not specified
            or the specified config is missing data points.
          app_name (string): The name of the application calling
            the service. Recommended for analytics purposes.
          app_version (string): The version of the application calling
            the service. Recommended for analytics purposes.
          lib_name (string): The API library software used for calling
            the service. (Unless you are writing an API client itself,
            leave this as default.)
          lib_version (string): The API library software version used
            for calling the service. (Unless you are writing an API client
            itself, leave this as default.)
          metrics_headers (dict): A dictionary of values for tracking
            client library metrics. Ultimately serializes to a string
            (e.g. 'foo/1.2.3 bar/3.14.1'). This argument should be
            considered private.

        Returns:
          A BenchmarkServiceClient object.
        """
        # Unless the calling application specifically requested
        # OAuth scopes, request everything.
        if scopes is None:
            scopes = self._ALL_SCOPES

        # Initialize an empty client config, if none is set.
        if client_config is None:
            client_config = {}

        # Initialize metrics_headers as an ordered dictionary
        # (cuts down on cardinality of the resulting string slightly).
        metrics_headers = collections.OrderedDict(metrics_headers)
        metrics_headers['gl-python'] = platform.python_version()

        # The library may or may not be set, depending on what is
        # calling this client. Newer client libraries set the library name
        # and version.
        if lib_name:
            metrics_headers[lib_name] = lib_version

        # Finally, track the GAPIC package version.
        # metrics_headers['gapic'] = pkg_resources.get_distribution(
        #     'gapic-testing-v0beta1',
        # ).version

        # Load the configuration defaults.
        default_client_config = json.loads(pkg_resources.resource_string(
            __name__, 'benchmark_service_client_config.json').decode())
        defaults = api_callable.construct_settings(
            'grpc.testing.BenchmarkService',
            default_client_config,
            client_config,
            config.STATUS_CODE_NAMES,
            metrics_headers=metrics_headers,
        )
        self.benchmark_service_stub = config.create_stub(
            services_pb2.BenchmarkServiceStub,
            channel=channel,
            service_path=service_path,
            service_port=port,
            credentials=credentials,
            scopes=scopes,
            ssl_credentials=ssl_credentials)

        self._unary_call = api_callable.create_api_call(
            self.benchmark_service_stub.UnaryCall,
            settings=defaults['unary_call'])
        self._streaming_call = api_callable.create_api_call(
            self.benchmark_service_stub.StreamingCall,
            settings=defaults['streaming_call'])
        self._streaming_from_client = api_callable.create_api_call(
            self.benchmark_service_stub.StreamingFromClient,
            settings=defaults['streaming_from_client'])
        self._streaming_from_server = api_callable.create_api_call(
            self.benchmark_service_stub.StreamingFromServer,
            settings=defaults['streaming_from_server'])
        self._streaming_both_ways = api_callable.create_api_call(
            self.benchmark_service_stub.StreamingBothWays,
            settings=defaults['streaming_both_ways'])

    # Service calls
    def unary_call(
            self,
            response_type=None,
            response_size=None,
            payload=None,
            fill_username=None,
            fill_oauth_scope=None,
            response_compressed=None,
            response_status=None,
            expect_compressed=None,
            options=None):
        """
        One request followed by one response.
        The server returns the client payload as-is.

        Example:
          >>> from grpc.testing.gapic import benchmark_service_client
          >>> client = benchmark_service_client.BenchmarkServiceClient()
          >>> response = client.unary_call()

        Args:
          response_type (enum :class:`grpc.testing.gapic.enums.PayloadType`): DEPRECATED, don't use. To be removed shortly.
            Desired payload type in the response from the server.
            If response_type is RANDOM, server randomly chooses one from other formats.
          response_size (int): Desired payload size in the response from the server.
          payload (:class:`grpc.testing.messages_pb2.Payload`): Optional input payload sent along with the request.
          fill_username (bool): Whether SimpleResponse should include username.
          fill_oauth_scope (bool): Whether SimpleResponse should include OAuth scope.
          response_compressed (:class:`grpc.testing.messages_pb2.BoolValue`): Whether to request the server to compress the response. This field is
            \"nullable\" in order to interoperate seamlessly with clients not able to
            implement the full compression tests by introspecting the call to verify
            the response's compression status.
          response_status (:class:`grpc.testing.messages_pb2.EchoStatus`): Whether server should return a given status
          expect_compressed (:class:`grpc.testing.messages_pb2.BoolValue`): Whether the server should expect this request to be compressed.
          options (:class:`google.gax.CallOptions`): Overrides the default
            settings for this call, e.g, timeout, retries etc.

        Returns:
          A :class:`grpc.testing.messages_pb2.SimpleResponse` instance.

        Raises:
          :exc:`google.gax.errors.GaxError` if the RPC is aborted.
          :exc:`ValueError` if the parameters are invalid.
        """
        # Create the request object.
        request = messages_pb2.SimpleRequest(
            response_type=response_type,
            response_size=response_size,
            payload=payload,
            fill_username=fill_username,
            fill_oauth_scope=fill_oauth_scope,
            response_compressed=response_compressed,
            response_status=response_status,
            expect_compressed=expect_compressed)
        return self._unary_call(request, options)

    def streaming_call(
            self,
            requests,
            options=None):
        """
        Repeated sequence of one request followed by one response.
        Should be called streaming ping-pong
        The server returns the client payload as-is on each response

        EXPERIMENTAL: This method interface might change in the future.

        Example:
          >>> from grpc.testing.gapic import benchmark_service_client
          >>> from grpc.testing import messages_pb2
          >>> client = benchmark_service_client.BenchmarkServiceClient()
          >>> request = messages_pb2.SimpleRequest()
          >>> requests = [request]
          >>> for element in client.streaming_call(requests):
          >>>     # process element
          >>>     pass

        Args:
          requests (iterator[:class:`grpc.testing.messages_pb2.SimpleRequest`]): The input objects.
          options (:class:`google.gax.CallOptions`): Overrides the default
            settings for this call, e.g, timeout, retries etc.

        Returns:
          iterator[:class:`grpc.testing.messages_pb2.SimpleResponse`].

        Raises:
          :exc:`google.gax.errors.GaxError` if the RPC is aborted.
          :exc:`ValueError` if the parameters are invalid.
        """
        return self._streaming_call(requests, options)

    def streaming_from_client(
            self,
            requests,
            options=None):
        """
        Single-sided unbounded streaming from client to server
        The server returns the client payload as-is once the client does WritesDone

        EXPERIMENTAL: This method interface might change in the future.

        Example:
          >>> from grpc.testing.gapic import benchmark_service_client
          >>> from grpc.testing import messages_pb2
          >>> client = benchmark_service_client.BenchmarkServiceClient()
          >>> request = messages_pb2.SimpleRequest()
          >>> requests = [request]
          >>> response = client.streaming_from_client(requests)

        Args:
          requests (iterator[:class:`grpc.testing.messages_pb2.SimpleRequest`]): The input objects.
          options (:class:`google.gax.CallOptions`): Overrides the default
            settings for this call, e.g, timeout, retries etc.

        Returns:
          A :class:`grpc.testing.messages_pb2.SimpleResponse` instance.

        Raises:
          :exc:`google.gax.errors.GaxError` if the RPC is aborted.
          :exc:`ValueError` if the parameters are invalid.
        """
        return self._streaming_from_client(requests, options)

    def streaming_from_server(
            self,
            response_type=None,
            response_size=None,
            payload=None,
            fill_username=None,
            fill_oauth_scope=None,
            response_compressed=None,
            response_status=None,
            expect_compressed=None,
            options=None):
        """
        Single-sided unbounded streaming from server to client
        The server repeatedly returns the client payload as-is

        Example:
          >>> from grpc.testing.gapic import benchmark_service_client
          >>> client = benchmark_service_client.BenchmarkServiceClient()
          >>> for element in client.streaming_from_server():
          >>>     # process element
          >>>     pass

        Args:
          response_type (enum :class:`grpc.testing.gapic.enums.PayloadType`): DEPRECATED, don't use. To be removed shortly.
            Desired payload type in the response from the server.
            If response_type is RANDOM, server randomly chooses one from other formats.
          response_size (int): Desired payload size in the response from the server.
          payload (:class:`grpc.testing.messages_pb2.Payload`): Optional input payload sent along with the request.
          fill_username (bool): Whether SimpleResponse should include username.
          fill_oauth_scope (bool): Whether SimpleResponse should include OAuth scope.
          response_compressed (:class:`grpc.testing.messages_pb2.BoolValue`): Whether to request the server to compress the response. This field is
            \"nullable\" in order to interoperate seamlessly with clients not able to
            implement the full compression tests by introspecting the call to verify
            the response's compression status.
          response_status (:class:`grpc.testing.messages_pb2.EchoStatus`): Whether server should return a given status
          expect_compressed (:class:`grpc.testing.messages_pb2.BoolValue`): Whether the server should expect this request to be compressed.
          options (:class:`google.gax.CallOptions`): Overrides the default
            settings for this call, e.g, timeout, retries etc.

        Returns:
          iterator[:class:`grpc.testing.messages_pb2.SimpleResponse`].

        Raises:
          :exc:`google.gax.errors.GaxError` if the RPC is aborted.
          :exc:`ValueError` if the parameters are invalid.
        """
        # Create the request object.
        request = messages_pb2.SimpleRequest(
            response_type=response_type,
            response_size=response_size,
            payload=payload,
            fill_username=fill_username,
            fill_oauth_scope=fill_oauth_scope,
            response_compressed=response_compressed,
            response_status=response_status,
            expect_compressed=expect_compressed)
        return self._streaming_from_server(request, options)

    def streaming_both_ways(
            self,
            requests,
            options=None):
        """
        Two-sided unbounded streaming between server to client
        Both sides send the content of their own choice to the other

        EXPERIMENTAL: This method interface might change in the future.

        Example:
          >>> from grpc.testing.gapic import benchmark_service_client
          >>> from grpc.testing import messages_pb2
          >>> client = benchmark_service_client.BenchmarkServiceClient()
          >>> request = messages_pb2.SimpleRequest()
          >>> requests = [request]
          >>> for element in client.streaming_both_ways(requests):
          >>>     # process element
          >>>     pass

        Args:
          requests (iterator[:class:`grpc.testing.messages_pb2.SimpleRequest`]): The input objects.
          options (:class:`google.gax.CallOptions`): Overrides the default
            settings for this call, e.g, timeout, retries etc.

        Returns:
          iterator[:class:`grpc.testing.messages_pb2.SimpleResponse`].

        Raises:
          :exc:`google.gax.errors.GaxError` if the RPC is aborted.
          :exc:`ValueError` if the parameters are invalid.
        """
        return self._streaming_both_ways(requests, options)
