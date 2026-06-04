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

from __future__ import absolute_import
from __future__ import division
from __future__ import print_function

import datetime
import select
import socket
import struct
import threading
import time

from tests.unit.framework.common import get_socket

_TCP_PROXY_BUFFER_SIZE = 1024
_TCP_PROXY_TIMEOUT = datetime.timedelta(milliseconds=500)


def _close_socket(sock):
    if sock is not None:
        try:
            sock.close()
        except socket.error:
            pass


def _init_proxy_socket(gateway_address, gateway_port):
    last_err = None
    for attempt in range(50):
        try:
            proxy_socket = socket.create_connection(
                (gateway_address, gateway_port),
                timeout=10.0
            )
            proxy_socket.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack('ii', 1, 0))
            proxy_socket.settimeout(None)
            return proxy_socket
        except (socket.error, TimeoutError) as err:
            last_err = err
            import logging
            logging.warning(
                "TcpProxy failed to connect to %s:%s (attempt %d/50): %s",
                gateway_address, gateway_port, attempt + 1, err
            )
            time.sleep(0.5 * (attempt + 1))
    raise last_err


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
            bind_address=self._bind_address
        )
        self._proxy_socket = _init_proxy_socket(
            self._gateway_address, self._gateway_port
        )
        self._thread.start()

    def get_port(self):
        return self._port

    def _handle_reads(self, sockets_to_read):
        for socket_to_read in sockets_to_read:
            try:
                if socket_to_read is self._listen_socket:
                    client_socket, client_address = socket_to_read.accept()
                    client_socket.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack('ii', 1, 0))
                    self._client_sockets.append(client_socket)
                elif socket_to_read is self._proxy_socket and self._proxy_socket is not None:
                    data = socket_to_read.recv(_TCP_PROXY_BUFFER_SIZE)
                    if data:
                        with self._byte_count_lock:
                            self._received_byte_count += len(data)
                        self._northbound_data += data
                    else:
                        self._proxy_socket.close()
                        self._proxy_socket = None
                elif socket_to_read in self._client_sockets:
                    data = socket_to_read.recv(_TCP_PROXY_BUFFER_SIZE)
                    if data:
                        with self._byte_count_lock:
                            self._sent_byte_count += len(data)
                        self._southbound_data += data
                    else:
                        self._client_sockets.remove(socket_to_read)
                        socket_to_read.close()
            except socket.error:
                if socket_to_read is self._listen_socket:
                    pass
                elif socket_to_read is self._proxy_socket and self._proxy_socket is not None:
                    _close_socket(self._proxy_socket)
                    self._proxy_socket = None
                elif socket_to_read in self._client_sockets:
                    if socket_to_read in self._client_sockets:
                        self._client_sockets.remove(socket_to_read)
                    _close_socket(socket_to_read)

    def _handle_writes(self, sockets_to_write):
        for socket_to_write in sockets_to_write:
            try:
                if socket_to_write is self._proxy_socket and self._proxy_socket is not None:
                    if self._southbound_data:
                        self._proxy_socket.sendall(self._southbound_data)
                        self._southbound_data = b""
                elif socket_to_write in self._client_sockets:
                    if self._northbound_data:
                        socket_to_write.sendall(self._northbound_data)
                        self._northbound_data = b""
            except socket.error:
                if socket_to_write is self._proxy_socket and self._proxy_socket is not None:
                    _close_socket(self._proxy_socket)
                    self._proxy_socket = None
                elif socket_to_write in self._client_sockets:
                    if socket_to_write in self._client_sockets:
                        self._client_sockets.remove(socket_to_write)
                    _close_socket(socket_to_write)

    def _cleanup_bad_sockets(self):
        if self._listen_socket is not None:
            try:
                select.select([self._listen_socket], [], [], 0)
            except (select.error, socket.error, ValueError):
                _close_socket(self._listen_socket)
                self._listen_socket = None

        if self._proxy_socket is not None:
            try:
                select.select([self._proxy_socket], [], [], 0)
            except (select.error, socket.error, ValueError):
                _close_socket(self._proxy_socket)
                self._proxy_socket = None

        valid_clients = []
        for s in self._client_sockets:
            try:
                select.select([s], [], [], 0)
                valid_clients.append(s)
            except (select.error, socket.error, ValueError):
                _close_socket(s)
        self._client_sockets = valid_clients

    def _run_proxy(self):
        try:
            while not self._stop_event.is_set():
                expected_reads = []
                if self._listen_socket is not None:
                    expected_reads.append(self._listen_socket)
                if self._proxy_socket is not None:
                    expected_reads.append(self._proxy_socket)
                expected_reads.extend(self._client_sockets)

                expected_writes = []
                if self._proxy_socket is not None:
                    expected_writes.append(self._proxy_socket)
                expected_writes.extend(self._client_sockets)

                if not expected_reads and not expected_writes:
                    break

                try:
                    sockets_to_read, sockets_to_write, _ = select.select(
                        expected_reads,
                        expected_writes,
                        (),
                        _TCP_PROXY_TIMEOUT.total_seconds(),
                    )
                except (select.error, socket.error, ValueError) as e:
                    if isinstance(e, ValueError):
                        import logging
                        import os
                        fd_count = len(os.listdir('/dev/fd')) if os.path.exists('/dev/fd') else -1
                        logging.error("TcpProxy select error: %s. FD count: %d", e, fd_count)
                    if self._stop_event.is_set():
                        break
                    self._cleanup_bad_sockets()
                    continue

                self._handle_reads(sockets_to_read)
                self._handle_writes(sockets_to_write)
        finally:
            for client_socket in list(self._client_sockets):
                _close_socket(client_socket)
            self._client_sockets.clear()

    def stop(self):
        self._stop_event.set()
        _close_socket(self._listen_socket)
        _close_socket(self._proxy_socket)
        for client_socket in list(self._client_sockets):
            _close_socket(client_socket)
        self._thread.join()

    def get_byte_count(self):
        with self._byte_count_lock:
            return self._sent_byte_count, self._received_byte_count

    def reset_byte_count(self):
        with self._byte_count_lock:
            self._sent_byte_count = 0
            self._received_byte_count = 0

    def __enter__(self):
        self.start()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.stop()
