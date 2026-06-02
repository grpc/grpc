# Copyright 2019 The gRPC authors.
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

import contextlib
import errno
import os
import socket
import sys

def _patch_grpc_localhost():
    if sys.platform != "darwin":
        return
    try:
        import grpc
    except ImportError:
        return
    if getattr(grpc, "_patched_localhost", False):
        return
    grpc._patched_localhost = True

    def patch_address(address):
        if not isinstance(address, str):
            return address
        if address.startswith("[::]:"):
            return address.replace("[::]:", "127.0.0.1:", 1)
        if address.startswith("localhost:"):
            return address.replace("localhost:", "127.0.0.1:", 1)
        if address == "[::]":
            return "127.0.0.1"
        if address == "localhost":
            return "127.0.0.1"
        return address

    orig_server = grpc.server
    def new_server(*args, **kwargs):
        options = list(kwargs.get('options') or [])
        if not any(k == 'grpc.so_reuseport' for k, _ in options):
            options.append(('grpc.so_reuseport', 0))
        kwargs['options'] = tuple(options)

        server = orig_server(*args, **kwargs)
        orig_add_insecure = server.add_insecure_port
        def new_add_insecure(address):
            return orig_add_insecure(patch_address(address))
        server.add_insecure_port = new_add_insecure
        
        orig_add_secure = server.add_secure_port
        def new_add_secure(address, *args, **kwargs):
            return orig_add_secure(patch_address(address), *args, **kwargs)
        server.add_secure_port = new_add_secure
        return server
    grpc.server = new_server

    orig_insecure_channel = grpc.insecure_channel
    def new_insecure_channel(target, *args, **kwargs):
        return orig_insecure_channel(patch_address(target), *args, **kwargs)
    grpc.insecure_channel = new_insecure_channel

    orig_secure_channel = grpc.secure_channel
    def new_secure_channel(target, *args, **kwargs):
        return orig_secure_channel(patch_address(target), *args, **kwargs)
    grpc.secure_channel = new_secure_channel

    def patch_aio(aio_module):
        orig_aio_server = aio_module.server
        def new_aio_server(*args, **kwargs):
            options = list(kwargs.get('options') or [])
            if not any(k == 'grpc.so_reuseport' for k, _ in options):
                options.append(('grpc.so_reuseport', 0))
            kwargs['options'] = tuple(options)

            server = orig_aio_server(*args, **kwargs)
            orig_add_insecure = server.add_insecure_port
            def new_add_insecure(address):
                return orig_add_insecure(patch_address(address))
            server.add_insecure_port = new_add_insecure
            
            orig_add_secure = server.add_secure_port
            def new_add_secure(address, *args, **kwargs):
                return orig_add_secure(patch_address(address), *args, **kwargs)
            server.add_secure_port = new_add_secure
            return server
        aio_module.server = new_aio_server

        orig_aio_insecure_channel = aio_module.insecure_channel
        def new_aio_insecure_channel(target, *args, **kwargs):
            return orig_aio_insecure_channel(patch_address(target), *args, **kwargs)
        aio_module.insecure_channel = new_aio_insecure_channel

        orig_aio_secure_channel = aio_module.secure_channel
        def new_aio_secure_channel(target, *args, **kwargs):
            return orig_aio_secure_channel(patch_address(target), *args, **kwargs)
        aio_module.secure_channel = new_aio_secure_channel

    try:
        from grpc.experimental import aio as exp_aio
        patch_aio(exp_aio)
    except (ImportError, AttributeError):
        pass

    try:
        import grpc.aio as public_aio
        patch_aio(public_aio)
    except (ImportError, AttributeError):
        pass

_patch_grpc_localhost()

_DEFAULT_SOCK_OPTIONS = (
    (socket.SO_REUSEADDR,)
    if os.name == "nt" or sys.platform == "darwin"
    else (socket.SO_REUSEADDR, socket.SO_REUSEPORT)
)
_UNRECOVERABLE_ERRNOS = (errno.EADDRINUSE, errno.ENOSR)


def get_socket(
    bind_address="localhost",
    port=0,
    listen=True,
    sock_options=_DEFAULT_SOCK_OPTIONS,
):
    if sys.platform == "darwin" and bind_address == "localhost":
        bind_address = "127.0.0.1"
    """Opens a socket.

    Useful for reserving a port for a system-under-test.

    Args:
      bind_address: The host to which to bind.
      port: The port to which to bind.
      listen: A boolean value indicating whether or not to listen on the socket.
      sock_options: A sequence of socket options to apply to the socket.

    Returns:
      A tuple containing:
        - the address to which the socket is bound
        - the port to which the socket is bound
        - the socket object itself
    """
    _sock_options = sock_options if sock_options else []
    if socket.has_ipv6:
        address_families = (socket.AF_INET6, socket.AF_INET)
    else:
        address_families = socket.AF_INET
    for address_family in address_families:
        try:
            sock = socket.socket(address_family, socket.SOCK_STREAM)
            for sock_option in _sock_options:
                sock.setsockopt(socket.SOL_SOCKET, sock_option, 1)
            sock.bind((bind_address, port))
            if listen:
                sock.listen(128)
            return bind_address, sock.getsockname()[1], sock
        except OSError as os_error:
            sock.close()
            if os_error.errno in _UNRECOVERABLE_ERRNOS:
                raise
            else:
                continue
        # For PY2, socket.error is a child class of IOError; for PY3, it is
        # pointing to OSError. We need this catch to make it 2/3 agnostic.
        except socket.error:  # pylint: disable=duplicate-except
            sock.close()
            continue
    raise RuntimeError(
        "Failed to bind to {} with sock_options {}".format(
            bind_address, sock_options
        )
    )


@contextlib.contextmanager
def bound_socket(
    bind_address="localhost",
    port=0,
    listen=True,
    sock_options=_DEFAULT_SOCK_OPTIONS,
):
    """Opens a socket bound to an arbitrary port.

    Useful for reserving a port for a system-under-test.

    Args:
      bind_address: The host to which to bind.
      port: The port to which to bind.
      listen: A boolean value indicating whether or not to listen on the socket.
      sock_options: A sequence of socket options to apply to the socket.

    Yields:
      A tuple containing:
        - the address to which the socket is bound
        - the port to which the socket is bound
    """
    host, port, sock = get_socket(
        bind_address=bind_address,
        port=port,
        listen=listen,
        sock_options=sock_options,
    )
    try:
        yield host, port
    finally:
        sock.close()
