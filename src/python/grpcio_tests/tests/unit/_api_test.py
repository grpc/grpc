# Copyright 2016, Google Inc.
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
"""Test of gRPC Python's application-layer API."""

import unittest

import six

import grpc

from tests.unit import _from_grpc_import_star


class AllTest(unittest.TestCase):

    def testAll(self):
        expected_grpc_code_elements = (
            'FutureTimeoutError', 'FutureCancelledError', 'Future',
            'ChannelConnectivity', 'StatusCode', 'RpcError', 'RpcContext',
            'Call', 'ChannelCredentials', 'CallCredentials',
            'AuthMetadataContext', 'AuthMetadataPluginCallback',
            'AuthMetadataPlugin', 'ServerCredentials',
            'UnaryUnaryMultiCallable', 'UnaryStreamMultiCallable',
            'StreamUnaryMultiCallable', 'StreamStreamMultiCallable', 'Channel',
            'ServicerContext', 'RpcMethodHandler', 'HandlerCallDetails',
            'GenericRpcHandler', 'ServiceRpcHandler', 'Server',
            'unary_unary_rpc_method_handler', 'unary_stream_rpc_method_handler',
            'stream_unary_rpc_method_handler',
            'stream_stream_rpc_method_handler',
            'method_handlers_generic_handler', 'ssl_channel_credentials',
            'metadata_call_credentials', 'access_token_call_credentials',
            'composite_call_credentials', 'composite_channel_credentials',
            'ssl_server_credentials', 'channel_ready_future',
            'insecure_channel', 'secure_channel', 'server',)

        six.assertCountEqual(self, expected_grpc_code_elements,
                             _from_grpc_import_star.GRPC_ELEMENTS)


class ChannelConnectivityTest(unittest.TestCase):

    def testChannelConnectivity(self):
        self.assertSequenceEqual(
            (grpc.ChannelConnectivity.IDLE, grpc.ChannelConnectivity.CONNECTING,
             grpc.ChannelConnectivity.READY,
             grpc.ChannelConnectivity.TRANSIENT_FAILURE,
             grpc.ChannelConnectivity.SHUTDOWN,),
            tuple(grpc.ChannelConnectivity))


class ChannelTest(unittest.TestCase):

    def test_secure_channel(self):
        channel_credentials = grpc.ssl_channel_credentials()
        channel = grpc.secure_channel('google.com:443', channel_credentials)


if __name__ == '__main__':
    unittest.main(verbosity=2)
