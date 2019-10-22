# Copyright 2019 gRPC authors.
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
"""gRPC's Asynchronous Python API."""

import abc
import types
import six

import grpc
from grpc._cython import cygrpc
from grpc._cython.cygrpc import init_grpc_aio
from ._server import server

from ._channel import Channel
from ._channel import UnaryUnaryMultiCallable


def insecure_channel(target, options=None, compression=None):
    """Creates an insecure asynchronous Channel to a server.

    Args:
      target: The server address
      options: An optional list of key-value pairs (channel args
        in gRPC Core runtime) to configure the channel.
      compression: An optional value indicating the compression method to be
        used over the lifetime of the channel. This is an EXPERIMENTAL option.

    Returns:
      A Channel.
    """
    from grpc.experimental.aio import _channel  # pylint: disable=cyclic-import
    return _channel.Channel(target, ()
                            if options is None else options, None, compression)


class _AioRpcError:
    """Private implementation of AioRpcError"""


class AioRpcError:
    """An RpcError to be used by the asynchronous API.

    Parent classes: (cygrpc._AioRpcError, RpcError)
    """
    # Dynamically registered as subclass of _AioRpcError and RpcError, because the former one is
    # only available after the cython code has been compiled.
    _class_built = _AioRpcError

    def __new__(cls, *args, **kwargs):
        if cls._class_built is _AioRpcError:
            cls._class_built = types.new_class(
                "AioRpcError", (cygrpc._AioRpcError, grpc.RpcError))
            cls._class_built.__doc__ = cls.__doc__

        return cls._class_built(*args, **kwargs)


###################################  __all__  #################################

__all__ = (
    'init_grpc_aio',
    'Channel',
    'UnaryUnaryMultiCallable',
    'insecure_channel',
    'AioRpcError',
)
