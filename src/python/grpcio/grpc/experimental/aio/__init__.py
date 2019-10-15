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

from grpc._cython.cygrpc import init_grpc_aio


class Channel(six.with_metaclass(abc.ABCMeta)):
    """Asynchronous Channel implementation."""

    @abc.abstractmethod
    def unary_unary(self,
                    method,
                    request_serializer=None,
                    response_deserializer=None):
        """Creates a UnaryUnaryMultiCallable for a unary-unary method.

        Args:
          method: The name of the RPC method.
          request_serializer: Optional behaviour for serializing the request
            message. Request goes unserialized in case None is passed.
          response_deserializer: Optional behaviour for deserializing the
            response message. Response goes undeserialized in case None
            is passed.

        Returns:
          A UnaryUnaryMultiCallable value for the named unary-unary method.
        """
        raise NotImplementedError()

    @abc.abstractmethod
    async def close(self):
        """Closes this Channel and releases all resources held by it.

        Closing the Channel will proactively terminate all RPCs active with the
        Channel and it is not valid to invoke new RPCs with the Channel.

        This method is idempotent.
        """
        raise NotImplementedError()

    @abc.abstractmethod
    async def __aenter__(self):
        """Starts an asynchronous context manager.

        Returns:
          Channel the channel that was instantiated.
        """
        raise NotImplementedError()

    @abc.abstractmethod
    async def __aexit__(self, exc_type, exc_val, exc_tb):
        """Finishes the asynchronous context manager by closing gracefully the channel."""
        raise NotImplementedError()


class UnaryUnaryMultiCallable(six.with_metaclass(abc.ABCMeta)):
    """Affords invoking a unary-unary RPC from client-side in an asynchronous way."""

    @abc.abstractmethod
    async def __call__(self,
                       request,
                       timeout=None,
                       metadata=None,
                       credentials=None,
                       wait_for_ready=None,
                       compression=None):
        """Asynchronously invokes the underlying RPC.

        Args:
          request: The request value for the RPC.
          timeout: An optional duration of time in seconds to allow
            for the RPC.
          metadata: Optional :term:`metadata` to be transmitted to the
            service-side of the RPC.
          credentials: An optional CallCredentials for the RPC. Only valid for
            secure Channel.
          wait_for_ready: This is an EXPERIMENTAL argument. An optional
            flag to enable wait for ready mechanism
          compression: An element of grpc.compression, e.g.
            grpc.compression.Gzip. This is an EXPERIMENTAL option.

        Returns:
          The response value for the RPC.

        Raises:
          RpcError: Indicating that the RPC terminated with non-OK status. The
            raised RpcError will also be a Call for the RPC affording the RPC's
            metadata, status code, and details.
        """
        raise NotImplementedError()


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
