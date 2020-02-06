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
import os
import socket
import errno

_DEFAULT_SOCK_OPTIONS = (socket.SO_REUSEADDR,
                         socket.SO_REUSEPORT) if os.name != 'nt' else (
                             socket.SO_REUSEADDR,)
_UNRECOVERABLE_ERRNOS = (errno.EADDRINUSE, errno.ENOSR)


def get_socket(bind_address='localhost',
               port=0,
               listen=True,
               sock_options=_DEFAULT_SOCK_OPTIONS):
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
        address_families = (socket.AF_INET)
    for address_family in address_families:
        try:
            sock = socket.socket(address_family, socket.SOCK_STREAM)
            for sock_option in _sock_options:
                sock.setsockopt(socket.SOL_SOCKET, sock_option, 1)
            sock.bind((bind_address, port))
            if listen:
                sock.listen(1)
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
    raise RuntimeError("Failed to bind to {} with sock_options {}".format(
        bind_address, sock_options))


@contextlib.contextmanager
def bound_socket(bind_address='localhost',
                 port=0,
                 listen=True,
                 sock_options=_DEFAULT_SOCK_OPTIONS):
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
    host, port, sock = get_socket(bind_address=bind_address,
                                  port=port,
                                  listen=listen,
                                  sock_options=sock_options)
    try:
        yield host, port
    finally:
        sock.close()
