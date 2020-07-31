# Copyright 2018 The gRPC Authors.
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

from concurrent import futures
import unittest
import logging

import grpc

from tests.unit import resources


class _ActualGenericRpcHandler(grpc.GenericRpcHandler):

    def service(self, handler_call_details):
        return None


class ServerTest(unittest.TestCase):

    def test_not_a_generic_rpc_handler_at_construction(self):
        with self.assertRaises(AttributeError) as exception_context:
            grpc.server(futures.ThreadPoolExecutor(max_workers=5),
                        handlers=[
                            _ActualGenericRpcHandler(),
                            object(),
                        ])
        self.assertIn('grpc.GenericRpcHandler',
                      str(exception_context.exception))

    def test_not_a_generic_rpc_handler_after_construction(self):
        server = grpc.server(futures.ThreadPoolExecutor(max_workers=5))
        with self.assertRaises(AttributeError) as exception_context:
            server.add_generic_rpc_handlers([
                _ActualGenericRpcHandler(),
                object(),
            ])
        self.assertIn('grpc.GenericRpcHandler',
                      str(exception_context.exception))

    def test_failed_port_binding_exception(self):
        server = grpc.server(None, options=(('grpc.so_reuseport', 0),))
        port = server.add_insecure_port('localhost:0')
        bind_address = "localhost:%d" % port

        with self.assertRaises(RuntimeError):
            server.add_insecure_port(bind_address)

        server_credentials = grpc.ssl_server_credentials([
            (resources.private_key(), resources.certificate_chain())
        ])
        with self.assertRaises(RuntimeError):
            server.add_secure_port(bind_address, server_credentials)


if __name__ == '__main__':
    logging.basicConfig()
    unittest.main(verbosity=2)
