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
import six

import grpc
from grpc import _common
from grpc._cython import cygrpc
from grpc._cython.cygrpc import init_grpc_aio

from ._call import AioRpcError
from ._call import Call
from ._channel import Channel
from ._channel import UnaryUnaryMultiCallable
from ._server import server


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
    return Channel(target, ()
                   if options is None else options, None, compression)


###################################  __all__  #################################

__all__ = ('AioRpcError', 'Call', 'init_grpc_aio', 'Channel',
           'UnaryUnaryMultiCallable', 'insecure_channel', 'server')
