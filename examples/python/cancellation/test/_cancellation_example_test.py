# Copyright 2019 the gRPC authors.
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
"""Test for cancellation example."""

import contextlib
import os
import signal
import socket
import subprocess
import unittest

_BINARY_DIR = os.path.realpath(
    os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
_SERVER_PATH = os.path.join(_BINARY_DIR, 'server')
_CLIENT_PATH = os.path.join(_BINARY_DIR, 'client')


@contextlib.contextmanager
def _get_port():
    sock = socket.socket(socket.AF_INET6, socket.SOCK_STREAM)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
    if sock.getsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT) == 0:
        raise RuntimeError("Failed to set SO_REUSEPORT.")
    sock.bind(('', 0))
    try:
        yield sock.getsockname()[1]
    finally:
        sock.close()


def _start_client(server_port,
                  desired_string,
                  ideal_distance,
                  interesting_distance=None):
    interesting_distance_args = () if interesting_distance is None else (
        '--show-inferior', interesting_distance)
    return subprocess.Popen((_CLIENT_PATH, desired_string, '--server',
                             'localhost:{}'.format(server_port),
                             '--ideal-distance', str(ideal_distance)) +
                            interesting_distance_args)


class CancellationExampleTest(unittest.TestCase):

    def test_successful_run(self):
        with _get_port() as test_port:
            server_process = subprocess.Popen(
                (_SERVER_PATH, '--port', str(test_port)))
            try:
                client_process = _start_client(test_port, 'aa', 0)
                client_return_code = client_process.wait()
                self.assertEqual(0, client_return_code)
                self.assertIsNone(server_process.poll())
            finally:
                server_process.kill()
                server_process.wait()

    def test_graceful_sigint(self):
        with _get_port() as test_port:
            server_process = subprocess.Popen(
                (_SERVER_PATH, '--port', str(test_port)))
            try:
                client_process1 = _start_client(test_port, 'aaaaaaaaaa', 0)
                client_process1.send_signal(signal.SIGINT)
                client_process1.wait()
                client_process2 = _start_client(test_port, 'aa', 0)
                client_return_code = client_process2.wait()
                self.assertEqual(0, client_return_code)
                self.assertIsNone(server_process.poll())
            finally:
                server_process.kill()
                server_process.wait()


if __name__ == '__main__':
    unittest.main(verbosity=2)
