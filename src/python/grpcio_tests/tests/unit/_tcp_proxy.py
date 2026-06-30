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
"""Proxies a TCP connection between a single client-server pair.

This proxy is not suitable for production, but should work well for cases in
which a test needs to spy on the bytes put on the wire between a server and
a client.
"""

import datetime
import select
import socket
import threading
import time

from tests.unit.framework.common import get_socket

_TCP_PROXY_BUFFER_SIZE = 1024
_TCP_PROXY_TIMEOUT = datetime.timedelta(milliseconds=500)


def _init_proxy_socket(gateway_address, gateway_port):
    for i in range(10):
        try:
            proxy_socket = socket.create_connection((gateway_address, gateway_port))
            return proxy_socket
        except Exception:
            if i == 9:
                raise
            time.sleep(0.5)


class TcpProxy:
    """Proxies a TCP connection between one client and one server."""

    def __init__(self, bind_address, gateway_address, gateway_port):
        self._bind_address = bind_address
        self._gateway_address = gateway_address
        self._gateway_port = gateway_port

        self._byte_count_lock = threading.RLock()
        self._sent_byte_count = 0
        self._received_byte_count = 0

        self._stop_event = threading.Event()

        self._port = None
        self._listen_socket = None
        self._proxy_socket = None

        # The following three attributes are owned by the serving thread.
        self._northbound_data = b""
        self._southbound_data = b""
        self._client_sockets = []

        self._thread = threading.Thread(target=self._run_proxy)

    def start(self):
        _, self._port, self._listen_socket = get_socket(
            bind_address=self._bind_address,
            sock_options=(socket.SO_REUSEADDR,),
        )
        self._proxy_socket = _init_proxy_socket(
            self._gateway_address, self._gateway_port
        )
        self._thread.start()

    def get_port(self):
        return self._port

    def _handle_reads(self, sockets_to_read):
        for socket_to_read in sockets_to_read:
            if socket_to_read is self._listen_socket:
                client_socket, client_address = socket_to_read.accept()
                self._client_sockets.append(client_socket)
            elif socket_to_read is self._proxy_socket:
                try:
                    data = socket_to_read.recv(_TCP_PROXY_BUFFER_SIZE)
                except OSError:
                    data = b""
                if data:
                    with self._byte_count_lock:
                        self._received_byte_count += len(data)
                    self._northbound_data += data
                else:
                    self._proxy_socket_read_eof = True
            elif socket_to_read in self._client_sockets:
                try:
                    data = socket_to_read.recv(_TCP_PROXY_BUFFER_SIZE)
                except OSError:
                    data = b""
                if data:
                    with self._byte_count_lock:
                        self._sent_byte_count += len(data)
                    self._southbound_data += data
                else:
                    socket_to_read.close()
                    self._client_sockets.remove(socket_to_read)
            else:
                raise RuntimeError("Unidentified socket appeared in read set.")

    def _handle_writes(self, sockets_to_write):
        for socket_to_write in sockets_to_write:
            try:
                if socket_to_write is self._proxy_socket:
                    if self._southbound_data:
                        self._proxy_socket.sendall(self._southbound_data)
                        self._southbound_data = b""
                elif socket_to_write in self._client_sockets:
                    if self._northbound_data:
                        socket_to_write.sendall(self._northbound_data)
                        self._northbound_data = b""
            except Exception:
                pass

    def _run_proxy(self):
        self._proxy_socket_read_eof = False
        while not self._stop_event.is_set():
            expected_reads = [self._listen_socket] + self._client_sockets
            if not self._proxy_socket_read_eof:
                expected_reads.append(self._proxy_socket)
            expected_writes = []
            if self._southbound_data:
                expected_writes.append(self._proxy_socket)
            if self._northbound_data:
                expected_writes.extend(self._client_sockets)
            sockets_to_read, sockets_to_write, _ = select.select(
                expected_reads,
                expected_writes,
                (),
                _TCP_PROXY_TIMEOUT.total_seconds(),
            )
            self._handle_reads(sockets_to_read)
            self._handle_writes(sockets_to_write)
        for client_socket in self._client_sockets:
            client_socket.close()

    def stop(self):
        self._stop_event.set()
        self._thread.join()
        self._listen_socket.close()
        self._proxy_socket.close()

    def get_byte_count(self):
        with self._byte_count_lock:
            return self._sent_byte_count, self._received_byte_count

    def reset_byte_count(self):
        with self._byte_count_lock:
            self._byte_count = 0
            self._received_byte_count = 0

    def __enter__(self):
        self.start()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.stop()
