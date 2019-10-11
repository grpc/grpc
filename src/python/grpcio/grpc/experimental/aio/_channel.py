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
from grpc.experimental import aio

SerializingFunction = Callable[[str], bytes]
DeserializingFunction = Callable[[bytes], str]


class UnaryUnaryMultiCallable(aio.UnaryUnaryMultiCallable):

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

    async def __call__(self,
                       request,
                       *,
                       timeout=None,
                       metadata=None,
                       credentials=None,
                       wait_for_ready=None,
                       compression=None):

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
        response = await self._channel.unary_unary(self._method,
                                                   serialized_request, timeout)
        return _common.deserialize(response, self._response_deserializer)


class Channel(aio.Channel):
    """A cygrpc.AioChannel-backed implementation of grpc.experimental.aio.Channel."""

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

        return UnaryUnaryMultiCallable(self._channel, _common.encode(method),
                                       request_serializer,
                                       response_deserializer)

    async def _close(self):
        # TODO: Send cancellation status
        self._channel.close()

    async def __aenter__(self):
        return self

    async def __aexit__(self, exc_type, exc_val, exc_tb):
        await self._close()

    async def close(self):
        await self._close()
