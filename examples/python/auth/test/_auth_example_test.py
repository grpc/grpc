# Copyright 2019 The gRPC Authors
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
"""Test for gRPC Python authentication example."""

import asyncio
import unittest

import grpc

from examples.python.auth import _credentials
from examples.python.auth import async_customized_auth_client
from examples.python.auth import async_customized_auth_server
from examples.python.auth import customized_auth_client
from examples.python.auth import customized_auth_server

_SERVER_ADDR_TEMPLATE = "localhost:%d"


class AuthExampleTest(unittest.TestCase):
    def test_successful_call(self):
        with customized_auth_server.run_server(0) as (_, port):
            with customized_auth_client.create_client_channel(
                _SERVER_ADDR_TEMPLATE % port
            ) as channel:
                customized_auth_client.send_rpc(channel)
        # No unhandled exception raised, test passed!

    def test_no_channel_credential(self):
        with customized_auth_server.run_server(0) as (_, port):
            with grpc.insecure_channel(_SERVER_ADDR_TEMPLATE % port) as channel:
                resp = customized_auth_client.send_rpc(channel)
                self.assertEqual(resp.code(), grpc.StatusCode.UNAVAILABLE)

    def test_no_call_credential(self):
        with customized_auth_server.run_server(0) as (_, port):
            channel_credential = grpc.ssl_channel_credentials(
                _credentials.ROOT_CERTIFICATE
            )
            with grpc.secure_channel(
                _SERVER_ADDR_TEMPLATE % port, channel_credential
            ) as channel:
                resp = customized_auth_client.send_rpc(channel)
                self.assertEqual(resp.code(), grpc.StatusCode.UNAUTHENTICATED)

    def test_successful_call_asyncio(self):
        async def test_body():
            server, port = await async_customized_auth_server.run_server(0)
            channel = async_customized_auth_client.create_client_channel(
                _SERVER_ADDR_TEMPLATE % port
            )
            await async_customized_auth_client.send_rpc(channel)
            await channel.close()
            await server.stop(0)
            # No unhandled exception raised, test passed!

        asyncio.get_event_loop().run_until_complete(test_body())


if __name__ == "__main__":
    unittest.main(verbosity=2)
