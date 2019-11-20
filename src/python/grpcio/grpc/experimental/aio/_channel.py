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
import functools
from typing import Callable, Optional, List, Sequence, Dict, TypeVar

from grpc import _common
from grpc._cython import cygrpc
from grpc._interceptor import _ClientCallDetails

from ._call import Call
from ._call import AioRpcError
from ._interceptor import ClientCallDetails
from ._interceptor import UnaryUnaryClientInterceptor

SerializingFunction = Callable[[str], bytes]
DeserializingFunction = Callable[[bytes], str]
Request = TypeVar('Request')


class UnaryUnaryMultiCallable:
    """Afford invoking a unary-unary RPC from client-side in an asynchronous way."""

    _channel: cygrpc.AioChannel
    _method: bytes
    _request_serializer: SerializingFunction
    _response_deserializer: DeserializingFunction
    _interceptors: Optional[List[UnaryUnaryClientInterceptor]]
    _loop: asyncio.AbstractEventLoop

    def __init__(
            self,
            channel: cygrpc.AioChannel,
            method: bytes,
            request_serializer: SerializingFunction,
            response_deserializer: DeserializingFunction,
            interceptors: Optional[List[UnaryUnaryClientInterceptor]] = None
    ) -> None:
        self._channel = channel
        self._method = method
        self._request_serializer = request_serializer
        self._response_deserializer = response_deserializer
        self._interceptors = interceptors
        self._loop = asyncio.get_event_loop()

    def _timeout_to_deadline(self, timeout: int) -> Optional[int]:
        if timeout is None:
            return None
        return self._loop.time() + timeout

    def __call__(self,
                 request: Request,
                 *,
                 timeout: Optional[float] = None,
                 metadata: Optional[Dict] = None,
                 credentials=None,
                 wait_for_ready: Optional[bool] = None,
                 compression: Optional[bool] = None) -> Call:
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
        aio_cancel_status = cygrpc.AioCancelStatus()

        if self._interceptors:
            client_call_details = _ClientCallDetails(
                self._method, timeout, metadata, credentials, wait_for_ready,
                compression)
            aio_call = asyncio.ensure_future(
                self._wrap_call_in_interceptors(
                    client_call_details, serialized_request, aio_cancel_status),
                loop=self._loop)
        else:
            aio_call = asyncio.ensure_future(
                self._call(self._method, serialized_request, timeout,
                           aio_cancel_status),
                loop=self._loop)

        return Call(aio_call, self._response_deserializer, aio_cancel_status)

    async def _wrap_call_in_interceptors(
            self, client_call_details: ClientCallDetails, request: bytes,
            aio_cancel_status: cygrpc.AioCancelStatus):
        """Run the RPC call wraped in interceptors"""

        async def _run_interceptor(
                interceptors: Sequence[UnaryUnaryClientInterceptor],
                client_call_details: ClientCallDetails, request: bytes):
            try:
                interceptor = next(interceptors)
            except StopIteration:
                interceptor = None

            if interceptor:
                continuation = functools.partial(_run_interceptor, interceptors)
                return await interceptor.intercept_unary_unary(
                    continuation, client_call_details, request)
            else:
                return await self._call(client_call_details.method, request,
                                        client_call_details.timeout,
                                        aio_cancel_status)

        return await _run_interceptor(
            iter(self._interceptors), client_call_details, request)

    async def _call(self, method: bytes, request: bytes,
                    timeout: Optional[float],
                    aio_cancel_status: cygrpc.AioCancelStatus):

        deadline = self._timeout_to_deadline(timeout)

        try:
            return await self._channel.unary_unary(method, request, deadline,
                                                   aio_cancel_status)
        except cygrpc.AioRpcError as aio_rpc_error:
            raise AioRpcError(
                _common.CYGRPC_STATUS_CODE_TO_STATUS_CODE[aio_rpc_error.code()],
                aio_rpc_error.details(), aio_rpc_error.initial_metadata(),
                aio_rpc_error.trailing_metadata()) from aio_rpc_error


class Channel:
    """Asynchronous Channel implementation.

    A cygrpc.AioChannel-backed implementation.
    """

    def __init__(self,
                 target,
                 options,
                 credentials,
                 compression,
                 interceptors=None):
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

        if options:
            raise NotImplementedError("TODO: options not implemented yet")

        if credentials:
            raise NotImplementedError("TODO: credentials not implemented yet")

        if compression:
            raise NotImplementedError("TODO: compression not implemented yet")

        if interceptors is None:
            self._unary_unary_interceptors = None
        else:
            self._unary_unary_interceptors = list(
                filter(
                    lambda interceptor: isinstance(interceptor, UnaryUnaryClientInterceptor),
                    interceptors)) or None

            invalid_interceptors = set(interceptors) - set(
                self._unary_unary_interceptors or [])
            if invalid_interceptors:
                raise ValueError(
                    "Interceptor must be "+\
                    "UnaryUnaryClientInterceptors, the following are invalid: {}"\
                    .format(invalid_interceptors))

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
        return UnaryUnaryMultiCallable(
            self._channel,
            _common.encode(method),
            request_serializer,
            response_deserializer,
            interceptors=self._unary_unary_interceptors)

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
