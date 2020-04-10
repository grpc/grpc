# Copyright 2020 The gRPC Authors
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
"""Test of RPCs made using ALTS credentials."""

import unittest
import os
from concurrent.futures import ThreadPoolExecutor
import grpc


class _GenericHandler(grpc.GenericRpcHandler):

    def service(self, handler_call_details):
        return grpc.unary_unary_rpc_method_handler(
            lambda request, unused_context: request)


class ALTSCredentialsTest(unittest.TestCase):

    def _create_server(self):
        server = grpc.server(ThreadPoolExecutor())
        server.add_generic_rpc_handlers((_GenericHandler(),))
        return server

    @unittest.skipIf(os.name == 'nt',
                     'TODO(https://github.com/grpc/grpc/issues/20078)')
    def test_alts(self):
        server_addr = 'localhost:{}'
        channel_creds = grpc.alts_channel_credentials(['svcacct@server.com'])
        server_creds = grpc.alts_server_credentials()

        server = self._create_server()
        port = server.add_secure_port(server_addr.format(0), server_creds)
        server.start()
        with grpc.secure_channel(server_addr.format(port),
                                 channel_creds) as channel:
            self.assertEqual(
                b'abc',
                channel.unary_unary('/test/method')(b'abc',
                                                    wait_for_ready=True))
        server.stop(None)


if __name__ == '__main__':
    unittest.main()
