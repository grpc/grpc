# Copyright 2021 The gRPC authors.
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
"""Tests xDS server and channel credentials."""

from concurrent import futures
import contextlib
import logging
import unittest

import grpc
import grpc.experimental

from tests.unit import resources
from tests.unit import test_common


class _GenericHandler(grpc.GenericRpcHandler):
    def service(self, handler_call_details):
        return grpc.unary_unary_rpc_method_handler(
            lambda request, unused_context: request
        )


@contextlib.contextmanager
def xds_channel_server_without_xds(server_fallback_creds):
    server = grpc.server(futures.ThreadPoolExecutor())
    server.add_generic_rpc_handlers((_GenericHandler(),))
    server_server_fallback_creds = grpc.ssl_server_credentials(
        ((resources.private_key(), resources.certificate_chain()),)
    )
    server_creds = grpc.xds_server_credentials(server_fallback_creds)
    port = server.add_secure_port("localhost:0", server_creds)
    server.start()
    try:
        yield "localhost:{}".format(port)
    finally:
        server.stop(None)


class XdsCredentialsTest(unittest.TestCase):
    def test_xds_creds_fallback_ssl(self):
        # Since there is no xDS server, the fallback credentials will be used.
        # In this case, SSL credentials.
        server_fallback_creds = grpc.ssl_server_credentials(
            ((resources.private_key(), resources.certificate_chain()),)
        )
        with xds_channel_server_without_xds(
            server_fallback_creds
        ) as server_address:
            override_options = (
                ("grpc.ssl_target_name_override", "foo.test.google.fr"),
            )
            channel_fallback_creds = grpc.ssl_channel_credentials(
                root_certificates=resources.test_root_certificates(),
                private_key=resources.private_key(),
                certificate_chain=resources.certificate_chain(),
            )
            channel_creds = grpc.xds_channel_credentials(channel_fallback_creds)
            with grpc.secure_channel(
                server_address, channel_creds, options=override_options
            ) as channel:
                request = b"abc"
                response = channel.unary_unary("/test/method")(
                    request, wait_for_ready=True
                )
                self.assertEqual(response, request)

    def test_xds_creds_fallback_insecure(self):
        # Since there is no xDS server, the fallback credentials will be used.
        # In this case, insecure.
        server_fallback_creds = grpc.insecure_server_credentials()
        with xds_channel_server_without_xds(
            server_fallback_creds
        ) as server_address:
            channel_fallback_creds = (
                grpc.experimental.insecure_channel_credentials()
            )
            channel_creds = grpc.xds_channel_credentials(channel_fallback_creds)
            with grpc.secure_channel(server_address, channel_creds) as channel:
                request = b"abc"
                response = channel.unary_unary("/test/method")(
                    request, wait_for_ready=True
                )
                self.assertEqual(response, request)

    def test_start_xds_server(self):
        server = grpc.server(futures.ThreadPoolExecutor(), xds=True)
        server.add_generic_rpc_handlers((_GenericHandler(),))
        server_fallback_creds = grpc.insecure_server_credentials()
        server_creds = grpc.xds_server_credentials(server_fallback_creds)
        port = server.add_secure_port("localhost:0", server_creds)
        server.start()
        server.stop(None)
        # No exceptions thrown. A more comprehensive suite of tests will be
        # provided by the interop tests.


if __name__ == "__main__":
    logging.basicConfig()
    unittest.main()
