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

from grpc import _common
from grpc._cython import cygrpc
from grpc.experimental import aio


class UnaryUnaryMultiCallable(aio.UnaryUnaryMultiCallable):

    def __init__(self, channel, method, request_serializer,
                 response_deserializer):
        self._channel = channel
        self._method = method
        self._request_serializer = request_serializer
        self._response_deserializer = response_deserializer

    async def __call__(self,
                       request,
                       timeout=None,
                       metadata=None,
                       credentials=None,
                       wait_for_ready=None,
                       compression=None):

        if timeout:
            raise NotImplementedError("TODO: timeout not implemented yet")

        if metadata:
            raise NotImplementedError("TODO: metadata not implemented yet")

        if credentials:
            raise NotImplementedError("TODO: credentials not implemented yet")

        if wait_for_ready:
            raise NotImplementedError(
                "TODO: wait_for_ready not implemented yet")

        if compression:
            raise NotImplementedError("TODO: compression not implemented yet")

        response = await self._channel.unary_unary(
            self._method, _common.serialize(request, self._request_serializer))

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
