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
"""Test of gRPC Python's application-layer API."""

import logging
import unittest

import grpc

from tests.unit import _from_grpc_import_star


class AllTest(unittest.TestCase):
    def testAll(self):
        expected_grpc_code_elements = (
            "FutureTimeoutError",
            "FutureCancelledError",
            "Future",
            "ChannelConnectivity",
            "Compression",
            "StatusCode",
            "Status",
            "RpcError",
            "RpcContext",
            "Call",
            "ChannelCredentials",
            "CallCredentials",
            "AuthMetadataContext",
            "AuthMetadataPluginCallback",
            "AuthMetadataPlugin",
            "ServerCertificateConfiguration",
            "ServerCredentials",
            "UnaryUnaryMultiCallable",
            "UnaryStreamMultiCallable",
            "StreamUnaryMultiCallable",
            "StreamStreamMultiCallable",
            "UnaryUnaryClientInterceptor",
            "UnaryStreamClientInterceptor",
            "StreamUnaryClientInterceptor",
            "StreamStreamClientInterceptor",
            "Channel",
            "ServicerContext",
            "RpcMethodHandler",
            "HandlerCallDetails",
            "GenericRpcHandler",
            "ServiceRpcHandler",
            "Server",
            "ServerInterceptor",
            "SyncAbortError",
            "SyncBaseError",
            "LocalConnectionType",
            "local_channel_credentials",
            "local_server_credentials",
            "alts_channel_credentials",
            "alts_server_credentials",
            "unary_unary_rpc_method_handler",
            "unary_stream_rpc_method_handler",
            "stream_unary_rpc_method_handler",
            "ClientCallDetails",
            "stream_stream_rpc_method_handler",
            "method_handlers_generic_handler",
            "ssl_channel_credentials",
            "metadata_call_credentials",
            "access_token_call_credentials",
            "composite_call_credentials",
            "composite_channel_credentials",
            "compute_engine_channel_credentials",
            "ssl_server_credentials",
            "ssl_server_certificate_configuration",
            "dynamic_ssl_server_credentials",
            "channel_ready_future",
            "insecure_channel",
            "secure_channel",
            "intercept_channel",
            "server",
            "protos",
            "services",
            "protos_and_services",
            "xds_channel_credentials",
            "xds_server_credentials",
            "insecure_server_credentials",
        )

        self.assertCountEqual(
            expected_grpc_code_elements, _from_grpc_import_star.GRPC_ELEMENTS
        )


class ChannelConnectivityTest(unittest.TestCase):
    def testChannelConnectivity(self):
        self.assertSequenceEqual(
            (
                grpc.ChannelConnectivity.IDLE,
                grpc.ChannelConnectivity.CONNECTING,
                grpc.ChannelConnectivity.READY,
                grpc.ChannelConnectivity.TRANSIENT_FAILURE,
                grpc.ChannelConnectivity.SHUTDOWN,
            ),
            tuple(grpc.ChannelConnectivity),
        )


class ChannelTest(unittest.TestCase):
    def test_secure_channel(self):
        channel_credentials = grpc.ssl_channel_credentials()
        channel = grpc.secure_channel("google.com:443", channel_credentials)
        channel.close()


if __name__ == "__main__":
    logging.basicConfig()
    unittest.main(verbosity=2)
