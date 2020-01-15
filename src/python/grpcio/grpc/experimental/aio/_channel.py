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
from typing import Any, AsyncIterable, Optional, Sequence, Text

import grpc
from grpc import _common
from grpc._cython import cygrpc

from . import _base_call
from ._call import (StreamStreamCall, StreamUnaryCall, UnaryStreamCall,
                    UnaryUnaryCall)
from ._interceptor import (InterceptedUnaryUnaryCall,
                           UnaryUnaryClientInterceptor)
from ._typing import (ChannelArgumentType, DeserializingFunction, MetadataType,
                      SerializingFunction)
from ._utils import _timeout_to_deadline


class _BaseMultiCallable:
    """Base class of all multi callable objects.

    Handles the initialization logic and stores common attributes.
    """
    _loop: asyncio.AbstractEventLoop
    _channel: cygrpc.AioChannel
    _method: bytes
    _request_serializer: SerializingFunction
    _response_deserializer: DeserializingFunction

    _channel: cygrpc.AioChannel
    _method: bytes
    _request_serializer: SerializingFunction
    _response_deserializer: DeserializingFunction
    _interceptors: Optional[Sequence[UnaryUnaryClientInterceptor]]
    _loop: asyncio.AbstractEventLoop

    def __init__(self, channel: cygrpc.AioChannel, method: bytes,
                 request_serializer: SerializingFunction,
                 response_deserializer: DeserializingFunction,
                 interceptors: Optional[Sequence[UnaryUnaryClientInterceptor]]
                ) -> None:
        self._loop = asyncio.get_event_loop()
        self._channel = channel
        self._method = method
        self._request_serializer = request_serializer
        self._response_deserializer = response_deserializer
        self._interceptors = interceptors


class UnaryUnaryMultiCallable(_BaseMultiCallable):
    """Factory an asynchronous unary-unary RPC stub call from client-side."""

    def __call__(self,
                 request: Any,
                 *,
                 timeout: Optional[float] = None,
                 metadata: Optional[MetadataType] = None,
                 credentials: Optional[grpc.CallCredentials] = None,
                 wait_for_ready: Optional[bool] = None,
                 compression: Optional[grpc.Compression] = None
                ) -> _base_call.UnaryUnaryCall:
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
        if wait_for_ready:
            raise NotImplementedError(
                "TODO: wait_for_ready not implemented yet")
        if compression:
            raise NotImplementedError("TODO: compression not implemented yet")

        if metadata is None:
            metadata = tuple()

        if not self._interceptors:
            return UnaryUnaryCall(
                request,
                _timeout_to_deadline(timeout),
                metadata,
                credentials,
                self._channel,
                self._method,
                self._request_serializer,
                self._response_deserializer,
            )
        else:
            return InterceptedUnaryUnaryCall(
                self._interceptors,
                request,
                timeout,
                metadata,
                credentials,
                self._channel,
                self._method,
                self._request_serializer,
                self._response_deserializer,
            )


class UnaryStreamMultiCallable(_BaseMultiCallable):
    """Affords invoking a unary-stream RPC from client-side in an asynchronous way."""

    def __call__(self,
                 request: Any,
                 *,
                 timeout: Optional[float] = None,
                 metadata: Optional[MetadataType] = None,
                 credentials: Optional[grpc.CallCredentials] = None,
                 wait_for_ready: Optional[bool] = None,
                 compression: Optional[grpc.Compression] = None
                ) -> _base_call.UnaryStreamCall:
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
        """
        if wait_for_ready:
            raise NotImplementedError(
                "TODO: wait_for_ready not implemented yet")

        if compression:
            raise NotImplementedError("TODO: compression not implemented yet")

        deadline = _timeout_to_deadline(timeout)
        if metadata is None:
            metadata = tuple()

        return UnaryStreamCall(
            request,
            deadline,
            metadata,
            credentials,
            self._channel,
            self._method,
            self._request_serializer,
            self._response_deserializer,
        )


class StreamUnaryMultiCallable(_BaseMultiCallable):
    """Affords invoking a stream-unary RPC from client-side in an asynchronous way."""

    def __call__(self,
                 request_async_iterator: Optional[AsyncIterable[Any]] = None,
                 timeout: Optional[float] = None,
                 metadata: Optional[MetadataType] = None,
                 credentials: Optional[grpc.CallCredentials] = None,
                 wait_for_ready: Optional[bool] = None,
                 compression: Optional[grpc.Compression] = None
                ) -> _base_call.StreamUnaryCall:
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
        if wait_for_ready:
            raise NotImplementedError(
                "TODO: wait_for_ready not implemented yet")

        if compression:
            raise NotImplementedError("TODO: compression not implemented yet")

        deadline = _timeout_to_deadline(timeout)
        if metadata is None:
            metadata = tuple()

        return StreamUnaryCall(
            request_async_iterator,
            deadline,
            metadata,
            credentials,
            self._channel,
            self._method,
            self._request_serializer,
            self._response_deserializer,
        )


class StreamStreamMultiCallable(_BaseMultiCallable):
    """Affords invoking a stream-stream RPC from client-side in an asynchronous way."""

    def __call__(self,
                 request_async_iterator: Optional[AsyncIterable[Any]] = None,
                 timeout: Optional[float] = None,
                 metadata: Optional[MetadataType] = None,
                 credentials: Optional[grpc.CallCredentials] = None,
                 wait_for_ready: Optional[bool] = None,
                 compression: Optional[grpc.Compression] = None
                ) -> _base_call.StreamStreamCall:
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
        if wait_for_ready:
            raise NotImplementedError(
                "TODO: wait_for_ready not implemented yet")

        if compression:
            raise NotImplementedError("TODO: compression not implemented yet")

        deadline = _timeout_to_deadline(timeout)
        if metadata is None:
            metadata = tuple()

        return StreamStreamCall(
            request_async_iterator,
            deadline,
            metadata,
            credentials,
            self._channel,
            self._method,
            self._request_serializer,
            self._response_deserializer,
        )


class Channel:
    """Asynchronous Channel implementation.

    A cygrpc.AioChannel-backed implementation.
    """
    _channel: cygrpc.AioChannel
    _unary_unary_interceptors: Optional[Sequence[UnaryUnaryClientInterceptor]]

    def __init__(self, target: Text, options: Optional[ChannelArgumentType],
                 credentials: Optional[grpc.ChannelCredentials],
                 compression: Optional[grpc.Compression],
                 interceptors: Optional[Sequence[UnaryUnaryClientInterceptor]]):
        """Constructor.

        Args:
          target: The target to which to connect.
          options: Configuration options for the channel.
          credentials: A cygrpc.ChannelCredentials or None.
          compression: An optional value indicating the compression method to be
            used over the lifetime of the channel.
          interceptors: An optional list of interceptors that would be used for
            intercepting any RPC executed with that channel.
        """

        if compression:
            raise NotImplementedError("TODO: compression not implemented yet")

        if interceptors is None:
            self._unary_unary_interceptors = None
        else:
            self._unary_unary_interceptors = list(
                filter(
                    lambda interceptor: isinstance(interceptor,
                                                   UnaryUnaryClientInterceptor),
                    interceptors))

            invalid_interceptors = set(interceptors) - set(
                self._unary_unary_interceptors)

            if invalid_interceptors:
                raise ValueError(
                    "Interceptor must be "+\
                    "UnaryUnaryClientInterceptors, the following are invalid: {}"\
                    .format(invalid_interceptors))

        self._channel = cygrpc.AioChannel(_common.encode(target), options,
                                          credentials)

    def get_state(self,
                  try_to_connect: bool = False) -> grpc.ChannelConnectivity:
        """Check the connectivity state of a channel.

        This is an EXPERIMENTAL API.

        If the channel reaches a stable connectivity state, it is guaranteed
        that the return value of this function will eventually converge to that
        state.

        Args: try_to_connect: a bool indicate whether the Channel should try to
          connect to peer or not.

        Returns: A ChannelConnectivity object.
        """
        result = self._channel.check_connectivity_state(try_to_connect)
        return _common.CYGRPC_CONNECTIVITY_STATE_TO_CHANNEL_CONNECTIVITY[result]

    async def wait_for_state_change(
            self,
            last_observed_state: grpc.ChannelConnectivity,
    ) -> None:
        """Wait for a change in connectivity state.

        This is an EXPERIMENTAL API.

        The function blocks until there is a change in the channel connectivity
        state from the "last_observed_state". If the state is already
        different, this function will return immediately.

        There is an inherent race between the invocation of
        "Channel.wait_for_state_change" and "Channel.get_state". The state can
        change arbitrary times during the race, so there is no way to observe
        every state transition.

        If there is a need to put a timeout for this function, please refer to
        "asyncio.wait_for".

        Args:
          last_observed_state: A grpc.ChannelConnectivity object representing
            the last known state.
        """
        assert await self._channel.watch_connectivity_state(
            last_observed_state.value[0], None)

    def unary_unary(
            self,
            method: Text,
            request_serializer: Optional[SerializingFunction] = None,
            response_deserializer: Optional[DeserializingFunction] = None
    ) -> UnaryUnaryMultiCallable:
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
                                       response_deserializer,
                                       self._unary_unary_interceptors)

    def unary_stream(
            self,
            method: Text,
            request_serializer: Optional[SerializingFunction] = None,
            response_deserializer: Optional[DeserializingFunction] = None
    ) -> UnaryStreamMultiCallable:
        return UnaryStreamMultiCallable(self._channel, _common.encode(method),
                                        request_serializer,
                                        response_deserializer, None)

    def stream_unary(
            self,
            method: Text,
            request_serializer: Optional[SerializingFunction] = None,
            response_deserializer: Optional[DeserializingFunction] = None
    ) -> StreamUnaryMultiCallable:
        return StreamUnaryMultiCallable(self._channel, _common.encode(method),
                                        request_serializer,
                                        response_deserializer, None)

    def stream_stream(
            self,
            method: Text,
            request_serializer: Optional[SerializingFunction] = None,
            response_deserializer: Optional[DeserializingFunction] = None
    ) -> StreamStreamMultiCallable:
        return StreamStreamMultiCallable(self._channel, _common.encode(method),
                                         request_serializer,
                                         response_deserializer, None)

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
