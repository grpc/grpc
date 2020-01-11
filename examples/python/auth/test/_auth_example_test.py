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

from __future__ import absolute_import
from __future__ import division
from __future__ import print_function

import unittest

import grpc
from examples.python.auth import _credentials
from examples.python.auth import customized_auth_client
from examples.python.auth import customized_auth_server

_SERVER_ADDR_TEMPLATE = 'localhost:%d'


class AuthExampleTest(unittest.TestCase):

    def test_successful_call(self):
        with customized_auth_server.run_server(0) as (_, port):
            with customized_auth_client.create_client_channel(
                    _SERVER_ADDR_TEMPLATE % port) as channel:
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
                _credentials.ROOT_CERTIFICATE)
            with grpc.secure_channel(_SERVER_ADDR_TEMPLATE % port,
                                     channel_credential) as channel:
                resp = customized_auth_client.send_rpc(channel)
                self.assertEqual(resp.code(), grpc.StatusCode.UNAUTHENTICATED)


if __name__ == '__main__':
    unittest.main(verbosity=2)
