# Copyright 2015 gRPC authors.
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
"""Common code used throughout tests of gRPC."""

import ast
import collections
from concurrent import futures
import os
import threading

import grpc

INVOCATION_INITIAL_METADATA = (
    ("0", "abc"),
    ("1", "def"),
    ("2", "ghi"),
)
SERVICE_INITIAL_METADATA = (
    ("3", "jkl"),
    ("4", "mno"),
    ("5", "pqr"),
)
SERVICE_TERMINAL_METADATA = (
    ("6", "stu"),
    ("7", "vwx"),
    ("8", "yza"),
)
DETAILS = "test details"


def metadata_transmitted(original_metadata, transmitted_metadata):
    """Judges whether or not metadata was acceptably transmitted.

    gRPC is allowed to insert key-value pairs into the metadata values given by
    applications and to reorder key-value pairs with different keys but it is not
    allowed to alter existing key-value pairs or to reorder key-value pairs with
    the same key.

    Args:
      original_metadata: A metadata value used in a test of gRPC. An iterable over
        iterables of length 2.
      transmitted_metadata: A metadata value corresponding to original_metadata
        after having been transmitted via gRPC. An iterable over iterables of
        length 2.

    Returns:
       A boolean indicating whether transmitted_metadata accurately reflects
        original_metadata after having been transmitted via gRPC.
    """
    original = collections.defaultdict(list)
    for key, value in original_metadata:
        original[key].append(value)
    transmitted = collections.defaultdict(list)
    for key, value in transmitted_metadata:
        transmitted[key].append(value)

    for key, values in original.items():
        transmitted_values = transmitted[key]
        transmitted_iterator = iter(transmitted_values)
        try:
            for value in values:
                while True:
                    transmitted_value = next(transmitted_iterator)
                    if value == transmitted_value:
                        break
        except StopIteration:
            return False
    else:
        return True


def test_secure_channel(target, channel_credentials, server_host_override):
    """Creates an insecure Channel to a remote host.

    Args:
      host: The name of the remote host to which to connect.
      port: The port of the remote host to which to connect.
      channel_credentials: The implementations.ChannelCredentials with which to
        connect.
      server_host_override: The target name used for SSL host name checking.

    Returns:
      An implementations.Channel to the remote host through which RPCs may be
        conducted.
    """
    channel = grpc.secure_channel(
        target,
        channel_credentials,
        (
            (
                "grpc.ssl_target_name_override",
                server_host_override,
            ),
        ),
    )
    return channel


def test_server(max_workers=10, reuse_port=False):
    """Creates an insecure grpc server.

    These servers have SO_REUSEPORT disabled to prevent cross-talk.
    """
    import os
    import socket
    import ast
    import grpc
    from concurrent import futures

    server_kwargs = os.environ.get("GRPC_ADDITIONAL_SERVER_KWARGS", "{}")
    server_kwargs = ast.literal_eval(server_kwargs)
    server = grpc.server(
        futures.ThreadPoolExecutor(max_workers=max_workers),
        options=(("grpc.so_reuseport", int(reuse_port)),),
        **server_kwargs,
    )

    original_add_insecure_port = server.add_insecure_port

    if not hasattr(grpc, "_uds_port_map"):
        grpc._uds_port_map = {}
        original_insecure_channel = grpc.insecure_channel
        original_secure_channel = grpc.secure_channel
        
        def custom_insecure_channel(target, options=None, compression=None):
            try:
                port = int(target.split(":")[-1])
                if port in grpc._uds_port_map:
                    target = grpc._uds_port_map[port]
            except Exception:
                pass
            return original_insecure_channel(target, options, compression)
            
        def custom_secure_channel(target, credentials, options=None, compression=None):
            try:
                port = int(target.split(":")[-1])
                if port in grpc._uds_port_map:
                    target = grpc._uds_port_map[port]
            except Exception:
                pass
            return original_secure_channel(target, credentials, options, compression)
            
        grpc.insecure_channel = custom_insecure_channel
        grpc.secure_channel = custom_secure_channel
        
        if getattr(grpc, "aio", None):
            original_aio_insecure_channel = getattr(grpc.aio, "insecure_channel", None)
            original_aio_secure_channel = getattr(grpc.aio, "secure_channel", None)
            if original_aio_insecure_channel:
                def custom_aio_insecure_channel(target, options=None, compression=None, interceptors=None):
                    try:
                        port = int(target.split(":")[-1])
                        if port in grpc._uds_port_map:
                            target = grpc._uds_port_map[port]
                    except Exception:
                        pass
                    return original_aio_insecure_channel(target, options, compression, interceptors)
                grpc.aio.insecure_channel = custom_aio_insecure_channel
                if getattr(grpc, "experimental", None) and getattr(grpc.experimental, "aio", None):
                    grpc.experimental.aio.insecure_channel = custom_aio_insecure_channel
            if original_aio_secure_channel:
                def custom_aio_secure_channel(target, credentials, options=None, compression=None, interceptors=None):
                    try:
                        port = int(target.split(":")[-1])
                        if port in grpc._uds_port_map:
                            target = grpc._uds_port_map[port]
                    except Exception:
                        pass
                    return original_aio_secure_channel(target, credentials, options, compression, interceptors)
                grpc.aio.secure_channel = custom_aio_secure_channel
                if getattr(grpc, "experimental", None) and getattr(grpc.experimental, "aio", None):
                    grpc.experimental.aio.secure_channel = custom_aio_secure_channel

    def custom_add_insecure_port(address):
        if address in ("127.0.0.1:0", "localhost:0", "[::]:0", "[::1]:0"):
            import tempfile, os, uuid
            sock_name = f"grpc_test_{uuid.uuid4().hex}.sock"
            uds_path = os.path.join(tempfile.gettempdir(), sock_name)
            
            fake_port = hash(sock_name) % 10000 + 40000
            while fake_port in grpc._uds_port_map:
                fake_port += 1
                
            grpc._uds_port_map[fake_port] = f"unix:{uds_path}"
            original_add_insecure_port(f"unix:{uds_path}")
            return fake_port
        return original_add_insecure_port(address)

    original_add_secure_port = getattr(server, "add_secure_port", None)
    if original_add_secure_port:
        def custom_add_secure_port(address, server_credentials):
            if address in ("127.0.0.1:0", "localhost:0", "[::]:0", "[::1]:0"):
                import tempfile, os, uuid
                sock_name = f"grpc_test_{uuid.uuid4().hex}.sock"
                uds_path = os.path.join(tempfile.gettempdir(), sock_name)
                
                fake_port = hash(sock_name) % 10000 + 50000
                while fake_port in grpc._uds_port_map:
                    fake_port += 1
                    
                grpc._uds_port_map[fake_port] = f"unix:{uds_path}"
                original_add_secure_port(f"unix:{uds_path}", server_credentials)
                return fake_port
            return original_add_secure_port(address, server_credentials)
        server.add_secure_port = custom_add_secure_port

    server.add_insecure_port = custom_add_insecure_port
    return server


class WaitGroup:
    def __init__(self, n=0):
        self.count = n
        self.cv = threading.Condition()

    def add(self, n):
        self.cv.acquire()
        self.count += n
        self.cv.release()

    def done(self):
        self.cv.acquire()
        self.count -= 1
        if self.count == 0:
            self.cv.notify_all()
        self.cv.release()

    def wait(self):
        self.cv.acquire()
        while self.count > 0:
            self.cv.wait()
        self.cv.release()


def running_under_gevent():
    try:
        from gevent import monkey
        import gevent.socket
    except ImportError:
        return False
    else:
        import socket

        return socket.socket is gevent.socket.socket
