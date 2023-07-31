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

import logging
import socket
import time
import unittest

import grpc
from grpc.framework.foundation import logging_pool

from tests.unit.framework.common import bound_socket
from tests.unit.framework.common import test_constants

_REQUEST = b"\x00\x00\x00"
_RESPONSE = b"\x00\x00\x01"

_UNARY_UNARY = "/test/UnaryUnary"


def _handle_unary_unary(unused_request, unused_servicer_context):
    return _RESPONSE


class ReconnectTest(unittest.TestCase):
    def test_reconnect(self):
        server_pool = logging_pool.pool(test_constants.THREAD_CONCURRENCY)
        handler = grpc.method_handlers_generic_handler(
            "test",
            {
                "UnaryUnary": grpc.unary_unary_rpc_method_handler(
                    _handle_unary_unary
                )
            },
        )
        options = (("grpc.so_reuseport", 1),)
        with bound_socket() as (host, port):
            addr = f"{host}:{port}"
            server = grpc.server(server_pool, (handler,), options=options)
            server.add_insecure_port(addr)
            server.start()
        channel = grpc.insecure_channel(addr)
        multi_callable = channel.unary_unary(_UNARY_UNARY)
        self.assertEqual(_RESPONSE, multi_callable(_REQUEST))
        server.stop(None)
        # By default, the channel connectivity is checked every 5s
        # GRPC_CLIENT_CHANNEL_BACKUP_POLL_INTERVAL_MS can be set to change
        # this.
        time.sleep(5.1)
        server = grpc.server(server_pool, (handler,), options=options)
        server.add_insecure_port(addr)
        server.start()
        self.assertEqual(_RESPONSE, multi_callable(_REQUEST))
        server.stop(None)
        channel.close()


if __name__ == "__main__":
    logging.basicConfig()
    unittest.main(verbosity=2)
