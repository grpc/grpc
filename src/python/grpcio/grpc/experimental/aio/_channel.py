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
"""Invocation-side implementation of gRPC Asyncio Python."""
import asyncio
from typing import Callable, Optional

from grpc import _common
from grpc._cython import cygrpc

from ._call import Call

SerializingFunction = Callable[[str], bytes]
DeserializingFunction = Callable[[bytes], str]


class UnaryUnaryMultiCallable:
    """Afford invoking a unary-unary RPC from client-side in an asynchronous way."""

    def __init__(self, channel: cygrpc.AioChannel, method: bytes,
                 request_serializer: SerializingFunction,
                 response_deserializer: DeserializingFunction) -> None:
        self._channel = channel
        self._method = method
        self._request_serializer = request_serializer
        self._response_deserializer = response_deserializer
        self._loop = asyncio.get_event_loop()

    def _timeout_to_deadline(self, timeout: int) -> Optional[int]:
        if timeout is None:
            return None
        return self._loop.time() + timeout

    def __call__(self,
                 request,
                 *,
                 timeout=None,
                 metadata=None,
                 credentials=None,
                 wait_for_ready=None,
                 compression=None) -> Call:
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
          A Call object instance which is an awaitable object.

        Raises:
          RpcError: Indicating that the RPC terminated with non-OK status. The
            raised RpcError will also be a Call for the RPC affording the RPC's
            metadata, status code, and details.
        """

        if metadata:
            raise NotImplementedError("TODO: metadata not implemented yet")

        if credentials:
            raise NotImplementedError("TODO: credentials not implemented yet")

        if wait_for_ready:
            raise NotImplementedError(
                "TODO: wait_for_ready not implemented yet")

        if compression:
            raise NotImplementedError("TODO: compression not implemented yet")

        serialized_request = _common.serialize(request,
                                               self._request_serializer)
        timeout = self._timeout_to_deadline(timeout)
        aio_cancel_status = cygrpc.AioCancelStatus()
        aio_call = asyncio.ensure_future(
            self._channel.unary_unary(self._method, serialized_request, timeout,
                                      aio_cancel_status),
            loop=self._loop)
        return Call(aio_call, self._response_deserializer, aio_cancel_status)


class Channel:
    """Asynchronous Channel implementation.

    A cygrpc.AioChannel-backed implementation.
    """

    def __init__(self, target, options, credentials, compression):
        """Constructor.

        Args:
          target: The target to which to connect.
          options: Configuration options for the channel.
          credentials: A cygrpc.ChannelCredentials or None.
          compression: An optional value indicating the compression method to be
            used over the lifetime of the channel.
        """

        if options:
            raise NotImplementedError("TODO: options not implemented yet")

        if credentials:
            raise NotImplementedError("TODO: credentials not implemented yet")

        if compression:
            raise NotImplementedError("TODO: compression not implemented yet")

        self._channel = cygrpc.AioChannel(_common.encode(target))

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
        return UnaryUnaryMultiCallable(self._channel, _common.encode(method),
                                       request_serializer,
                                       response_deserializer)

    async def _close(self):
        # TODO: Send cancellation status
        self._channel.close()

    async def __aenter__(self):
        """Starts an asynchronous context manager.

        Returns:
          Channel the channel that was instantiated.
        """
        return self

    async def __aexit__(self, exc_type, exc_val, exc_tb):
        """Finishes the asynchronous context manager by closing gracefully the channel."""
        await self._close()

    async def close(self):
        """Closes this Channel and releases all resources held by it.

        Closing the Channel will proactively terminate all RPCs active with the
        Channel and it is not valid to invoke new RPCs with the Channel.

        This method is idempotent.
        """
        await self._close()
