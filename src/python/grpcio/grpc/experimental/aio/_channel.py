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
from typing import Any, AsyncIterable, Optional, Sequence, AbstractSet
from weakref import WeakSet

import logging
import grpc
from grpc import _common
from grpc._cython import cygrpc
from grpc import _compression
from grpc import _grpcio_metadata

from . import _base_call
from ._call import (StreamStreamCall, StreamUnaryCall, UnaryStreamCall,
                    UnaryUnaryCall)
from ._interceptor import (InterceptedUnaryUnaryCall,
                           UnaryUnaryClientInterceptor)
from ._typing import (ChannelArgumentType, DeserializingFunction, MetadataType,
                      SerializingFunction)
from ._utils import _timeout_to_deadline

_IMMUTABLE_EMPTY_TUPLE = tuple()
_USER_AGENT = 'grpc-python-asyncio/{}'.format(_grpcio_metadata.__version__)


def _augment_channel_arguments(base_options: ChannelArgumentType,
                               compression: Optional[grpc.Compression]):
    compression_channel_argument = _compression.create_channel_option(
        compression)
    user_agent_channel_argument = ((
        cygrpc.ChannelArgKey.primary_user_agent_string,
        _USER_AGENT,
    ),)
    return tuple(base_options
                ) + compression_channel_argument + user_agent_channel_argument


_LOGGER = logging.getLogger(__name__)


class _OngoingCalls:
    """Internal class used for have visibility of the ongoing calls."""

    _calls: AbstractSet[_base_call.RpcContext]

    def __init__(self):
        self._calls = WeakSet()

    def _remove_call(self, call: _base_call.RpcContext):
        try:
            self._calls.remove(call)
        except KeyError:
            pass

    @property
    def calls(self) -> AbstractSet[_base_call.RpcContext]:
        """Returns the set of ongoing calls."""
        return self._calls

    def size(self) -> int:
        """Returns the number of ongoing calls."""
        return len(self._calls)

    def trace_call(self, call: _base_call.RpcContext):
        """Adds and manages a new ongoing call."""
        self._calls.add(call)
        call.add_done_callback(self._remove_call)


class _BaseMultiCallable:
    """Base class of all multi callable objects.

    Handles the initialization logic and stores common attributes.
    """
    _loop: asyncio.AbstractEventLoop
    _channel: cygrpc.AioChannel
    _ongoing_calls: _OngoingCalls
    _method: bytes
    _request_serializer: SerializingFunction
    _response_deserializer: DeserializingFunction

    _channel: cygrpc.AioChannel
    _method: bytes
    _request_serializer: SerializingFunction
    _response_deserializer: DeserializingFunction
    _interceptors: Optional[Sequence[UnaryUnaryClientInterceptor]]
    _loop: asyncio.AbstractEventLoop

    # pylint: disable=too-many-arguments
    def __init__(
            self,
            channel: cygrpc.AioChannel,
            ongoing_calls: _OngoingCalls,
            method: bytes,
            request_serializer: SerializingFunction,
            response_deserializer: DeserializingFunction,
            interceptors: Optional[Sequence[UnaryUnaryClientInterceptor]],
            loop: asyncio.AbstractEventLoop,
    ) -> None:
        self._loop = loop
        self._channel = channel
        self._ongoing_calls = ongoing_calls
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
                 metadata: Optional[MetadataType] = _IMMUTABLE_EMPTY_TUPLE,
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
        if compression:
            metadata = _compression.augment_metadata(metadata, compression)

        if not self._interceptors:
            call = UnaryUnaryCall(request, _timeout_to_deadline(timeout),
                                  metadata, credentials, wait_for_ready,
                                  self._channel, self._method,
                                  self._request_serializer,
                                  self._response_deserializer, self._loop)
        else:
            call = InterceptedUnaryUnaryCall(
                self._interceptors, request, timeout, metadata, credentials,
                wait_for_ready, self._channel, self._method,
                self._request_serializer, self._response_deserializer,
                self._loop)

        self._ongoing_calls.trace_call(call)
        return call


class UnaryStreamMultiCallable(_BaseMultiCallable):
    """Affords invoking a unary-stream RPC from client-side in an asynchronous way."""

    def __call__(self,
                 request: Any,
                 *,
                 timeout: Optional[float] = None,
                 metadata: Optional[MetadataType] = _IMMUTABLE_EMPTY_TUPLE,
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
        if compression:
            metadata = _compression.augment_metadata(metadata, compression)

        deadline = _timeout_to_deadline(timeout)

        call = UnaryStreamCall(request, deadline, metadata, credentials,
                               wait_for_ready, self._channel, self._method,
                               self._request_serializer,
                               self._response_deserializer, self._loop)
        self._ongoing_calls.trace_call(call)
        return call


class StreamUnaryMultiCallable(_BaseMultiCallable):
    """Affords invoking a stream-unary RPC from client-side in an asynchronous way."""

    def __call__(self,
                 request_async_iterator: Optional[AsyncIterable[Any]] = None,
                 timeout: Optional[float] = None,
                 metadata: Optional[MetadataType] = _IMMUTABLE_EMPTY_TUPLE,
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
        if compression:
            metadata = _compression.augment_metadata(metadata, compression)

        deadline = _timeout_to_deadline(timeout)

        call = StreamUnaryCall(request_async_iterator, deadline, metadata,
                               credentials, wait_for_ready, self._channel,
                               self._method, self._request_serializer,
                               self._response_deserializer, self._loop)
        self._ongoing_calls.trace_call(call)
        return call


class StreamStreamMultiCallable(_BaseMultiCallable):
    """Affords invoking a stream-stream RPC from client-side in an asynchronous way."""

    def __call__(self,
                 request_async_iterator: Optional[AsyncIterable[Any]] = None,
                 timeout: Optional[float] = None,
                 metadata: Optional[MetadataType] = _IMMUTABLE_EMPTY_TUPLE,
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
        if compression:
            metadata = _compression.augment_metadata(metadata, compression)

        deadline = _timeout_to_deadline(timeout)

        call = StreamStreamCall(request_async_iterator, deadline, metadata,
                                credentials, wait_for_ready, self._channel,
                                self._method, self._request_serializer,
                                self._response_deserializer, self._loop)
        self._ongoing_calls.trace_call(call)
        return call


class Channel:
    """Asynchronous Channel implementation.

    A cygrpc.AioChannel-backed implementation.
    """
    _loop: asyncio.AbstractEventLoop
    _channel: cygrpc.AioChannel
    _unary_unary_interceptors: Optional[Sequence[UnaryUnaryClientInterceptor]]
    _ongoing_calls: _OngoingCalls

    def __init__(self, target: str, options: ChannelArgumentType,
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

        self._loop = asyncio.get_event_loop()
        self._channel = cygrpc.AioChannel(
            _common.encode(target),
            _augment_channel_arguments(options, compression), credentials,
            self._loop)
        self._ongoing_calls = _OngoingCalls()

    async def __aenter__(self):
        """Starts an asynchronous context manager.

        Returns:
          Channel the channel that was instantiated.
        """
        return self

    async def __aexit__(self, exc_type, exc_val, exc_tb):
        """Finishes the asynchronous context manager by closing the channel.

        Still active RPCs will be cancelled.
        """
        await self._close(None)

    async def _close(self, grace):
        if self._channel.closed():
            return

        # No new calls will be accepted by the Cython channel.
        self._channel.closing()

        if grace:
            # pylint: disable=unused-variable
            _, pending = await asyncio.wait(self._ongoing_calls.calls,
                                            timeout=grace,
                                            loop=self._loop)

            if not pending:
                return

        # A new set is created acting as a shallow copy because
        # when cancellation happens the calls are automatically
        # removed from the originally set.
        calls = WeakSet(data=self._ongoing_calls.calls)
        for call in calls:
            call.cancel()

        self._channel.close()

    async def close(self, grace: Optional[float] = None):
        """Closes this Channel and releases all resources held by it.

        This method immediately stops the channel from executing new RPCs in
        all cases.

        If a grace period is specified, this method wait until all active
        RPCs are finshed, once the grace period is reached the ones that haven't
        been terminated are cancelled. If a grace period is not specified
        (by passing None for grace), all existing RPCs are cancelled immediately.

        This method is idempotent.
        """
        await self._close(grace)

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

    async def channel_ready(self) -> None:
        """Creates a coroutine that ends when a Channel is ready."""
        state = self.get_state(try_to_connect=True)
        while state != grpc.ChannelConnectivity.READY:
            await self.wait_for_state_change(state)
            state = self.get_state(try_to_connect=True)

    def unary_unary(
            self,
            method: str,
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
        return UnaryUnaryMultiCallable(self._channel, self._ongoing_calls,
                                       _common.encode(method),
                                       request_serializer,
                                       response_deserializer,
                                       self._unary_unary_interceptors,
                                       self._loop)

    def unary_stream(
            self,
            method: str,
            request_serializer: Optional[SerializingFunction] = None,
            response_deserializer: Optional[DeserializingFunction] = None
    ) -> UnaryStreamMultiCallable:
        return UnaryStreamMultiCallable(self._channel, self._ongoing_calls,
                                        _common.encode(method),
                                        request_serializer,
                                        response_deserializer, None, self._loop)

    def stream_unary(
            self,
            method: str,
            request_serializer: Optional[SerializingFunction] = None,
            response_deserializer: Optional[DeserializingFunction] = None
    ) -> StreamUnaryMultiCallable:
        return StreamUnaryMultiCallable(self._channel, self._ongoing_calls,
                                        _common.encode(method),
                                        request_serializer,
                                        response_deserializer, None, self._loop)

    def stream_stream(
            self,
            method: str,
            request_serializer: Optional[SerializingFunction] = None,
            response_deserializer: Optional[DeserializingFunction] = None
    ) -> StreamStreamMultiCallable:
        return StreamStreamMultiCallable(self._channel, self._ongoing_calls,
                                         _common.encode(method),
                                         request_serializer,
                                         response_deserializer, None,
                                         self._loop)
