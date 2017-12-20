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
"""Tests that a channel will reconnect if a connection is dropped"""

import socket
import unittest

import grpc
from grpc.framework.foundation import logging_pool

from tests.unit.framework.common import test_constants

_REQUEST = b'\x00\x00\x00'
_RESPONSE = b'\x00\x00\x01'

_UNARY_UNARY = '/test/UnaryUnary'


def _handle_unary_unary(unused_request, unused_servicer_context):
    return _RESPONSE


class ReconnectTest(unittest.TestCase):

    def test_reconnect(self):
        server_pool = logging_pool.pool(test_constants.THREAD_CONCURRENCY)
        handler = grpc.method_handlers_generic_handler('test', {
            'UnaryUnary':
            grpc.unary_unary_rpc_method_handler(_handle_unary_unary)
        })
        # Reserve a port, when we restart the server we want
        # to hold onto the port
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        try:
            opt = socket.SO_REUSEPORT
        except AttributeError:
            # SO_REUSEPORT is unavailable on Windows, but SO_REUSEADDR
            # allows forcibly re-binding to a port
            opt = socket.SO_REUSEADDR
        s.setsockopt(socket.SOL_SOCKET, opt, 1)
        s.bind(('localhost', 0))
        port = s.getsockname()[1]

        server = grpc.server(server_pool, (handler,))
        server.add_insecure_port('[::]:{}'.format(port))
        server.start()
        channel = grpc.insecure_channel('localhost:%d' % port)
        multi_callable = channel.unary_unary(_UNARY_UNARY)
        self.assertEqual(_RESPONSE, multi_callable(_REQUEST))
        server.stop(None)
        server = grpc.server(server_pool, (handler,))
        server.add_insecure_port('[::]:{}'.format(port))
        server.start()
        self.assertEqual(_RESPONSE, multi_callable(_REQUEST))


if __name__ == '__main__':
    unittest.main(verbosity=2)
