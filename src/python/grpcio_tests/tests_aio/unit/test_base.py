# Copyright 2019 The gRPC Authors.
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

import os
import sys
import subprocess

import asyncio
import unittest
import socket

from grpc.experimental import aio
from tests_aio.unit import sync_server


def _get_free_loopback_tcp_port():
    if socket.has_ipv6:
        tcp_socket = socket.socket(socket.AF_INET6)
        host = "::1"
        host_target = "[::1]"
    else:
        tcp_socket = socket.socket(socket.AF_INET)
        host = "127.0.0.1"
        host_target = host
    tcp_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
    tcp_socket.bind((host, 0))
    address_tuple = tcp_socket.getsockname()
    return tcp_socket, "%s:%s" % (host_target, address_tuple[1])


class _Server:
    """_Server is an wrapper for a sync-server subprocess.

    The synchronous server is executed in another process which initializes
    implicitly the grpc using the synchronous configuration. Both worlds
    can not coexist within the same process.
    """

    def __init__(self, host_and_port):  # pylint: disable=W0621
        self._host_and_port = host_and_port
        self._handle = None

    def start(self):
        assert self._handle is None

        try:
            from google3.pyglib import resources
            executable = resources.GetResourceFilename(
                "google3/third_party/py/grpc/sync_server")
            args = [executable, '--host_and_port', self._host_and_port]
        except ImportError:
            executable = sys.executable
            directory, _ = os.path.split(os.path.abspath(__file__))
            filename = directory + '/sync_server.py'
            args = [
                executable, filename, '--host_and_port', self._host_and_port
            ]

        self._handle = subprocess.Popen(args)

    def terminate(self):
        if not self._handle:
            return

        self._handle.terminate()
        self._handle.wait()
        self._handle = None


class AioTestBase(unittest.TestCase):

    def setUp(self):
        self._socket, self._target = _get_free_loopback_tcp_port()
        self._server = _Server(self._target)
        self._server.start()
        self._loop = asyncio.new_event_loop()
        asyncio.set_event_loop(self._loop)
        aio.init_grpc_aio()

    def tearDown(self):
        self._server.terminate()
        self._socket.close()

    @property
    def loop(self):
        return self._loop

    @property
    def server_target(self):
        return self._target
