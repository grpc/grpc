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
import time
import unittest

import grpc
from grpc.framework.foundation import logging_pool

from tests.unit.framework.common import test_constants

_REQUEST = b'\x00\x00\x00'
_RESPONSE = b'\x00\x00\x01'

_UNARY_UNARY = '/test/UnaryUnary'


def _handle_unary_unary(unused_request, unused_servicer_context):
    return _RESPONSE


def _get_reuse_socket_option():
    try:
        return socket.SO_REUSEPORT
    except AttributeError:
        # SO_REUSEPORT is unavailable on Windows, but SO_REUSEADDR
        # allows forcibly re-binding to a port
        return socket.SO_REUSEADDR


def _pick_and_bind_port(sock_opt):
    # Reserve a port, when we restart the server we want
    # to hold onto the port
    port = 0
    for address_family in (socket.AF_INET6, socket.AF_INET):
        try:
            s = socket.socket(address_family, socket.SOCK_STREAM)
        except socket.error:
            continue  # this address family is unavailable
        s.setsockopt(socket.SOL_SOCKET, sock_opt, 1)
        try:
            s.bind(('localhost', port))
            # for socket.SOCK_STREAM sockets, it is necessary to call
            # listen to get the desired behavior.
            s.listen(1)
            port = s.getsockname()[1]
        except socket.error:
            # port was not available on the current address family
            # try again
            port = 0
            break
        finally:
            s.close()
    if s:
        return port if port != 0 else _pick_and_bind_port(sock_opt)
    else:
        return None  # no address family was available


class ReconnectTest(unittest.TestCase):

    def test_reconnect(self):
        server_pool = logging_pool.pool(test_constants.THREAD_CONCURRENCY)
        handler = grpc.method_handlers_generic_handler('test', {
            'UnaryUnary':
            grpc.unary_unary_rpc_method_handler(_handle_unary_unary)
        })
        sock_opt = _get_reuse_socket_option()
        port = _pick_and_bind_port(sock_opt)
        self.assertIsNotNone(port)

        server = grpc.server(server_pool, (handler,))
        server.add_insecure_port('[::]:{}'.format(port))
        server.start()
        channel = grpc.insecure_channel('localhost:%d' % port)
        multi_callable = channel.unary_unary(_UNARY_UNARY)
        self.assertEqual(_RESPONSE, multi_callable(_REQUEST))
        server.stop(None)
        # By default, the channel connectivity is checked every 5s
        # GRPC_CLIENT_CHANNEL_BACKUP_POLL_INTERVAL_MS can be set to change
        # this.
        time.sleep(5.1)
        server = grpc.server(server_pool, (handler,))
        server.add_insecure_port('[::]:{}'.format(port))
        server.start()
        self.assertEqual(_RESPONSE, multi_callable(_REQUEST))


if __name__ == '__main__':
    unittest.main(verbosity=2)
