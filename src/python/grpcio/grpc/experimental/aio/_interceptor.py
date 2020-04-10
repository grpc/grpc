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
"""Interceptors implementation of gRPC Asyncio Python."""
import asyncio
import collections
import functools
from abc import ABCMeta, abstractmethod
from typing import Callable, Optional, Iterator, Sequence, Union, Awaitable

import grpc
from grpc._cython import cygrpc

from . import _base_call
from ._call import UnaryUnaryCall, AioRpcError
from ._utils import _timeout_to_deadline
from ._typing import (RequestType, SerializingFunction, DeserializingFunction,
                      MetadataType, ResponseType, DoneCallbackType)

_LOCAL_CANCELLATION_DETAILS = 'Locally cancelled by application!'


class ServerInterceptor(metaclass=ABCMeta):
    """Affords intercepting incoming RPCs on the service-side.

    This is an EXPERIMENTAL API.
    """

    @abstractmethod
    async def intercept_service(
            self, continuation: Callable[[grpc.HandlerCallDetails], Awaitable[
                grpc.RpcMethodHandler]],
            handler_call_details: grpc.HandlerCallDetails
    ) -> grpc.RpcMethodHandler:
        """Intercepts incoming RPCs before handing them over to a handler.

        Args:
            continuation: A function that takes a HandlerCallDetails and
                proceeds to invoke the next interceptor in the chain, if any,
                or the RPC handler lookup logic, with the call details passed
                as an argument, and returns an RpcMethodHandler instance if
                the RPC is considered serviced, or None otherwise.
            handler_call_details: A HandlerCallDetails describing the RPC.

        Returns:
            An RpcMethodHandler with which the RPC may be serviced if the
            interceptor chooses to service this RPC, or None otherwise.
        """


class ClientCallDetails(
        collections.namedtuple(
            'ClientCallDetails',
            ('method', 'timeout', 'metadata', 'credentials', 'wait_for_ready')),
        grpc.ClientCallDetails):
    """Describes an RPC to be invoked.

    This is an EXPERIMENTAL API.

    Args:
        method: The method name of the RPC.
        timeout: An optional duration of time in seconds to allow for the RPC.
        metadata: Optional metadata to be transmitted to the service-side of
          the RPC.
        credentials: An optional CallCredentials for the RPC.
        wait_for_ready: This is an EXPERIMENTAL argument. An optional flag to
          enable wait for ready mechanism.
    """

    method: str
    timeout: Optional[float]
    metadata: Optional[MetadataType]
    credentials: Optional[grpc.CallCredentials]
    wait_for_ready: Optional[bool]


class UnaryUnaryClientInterceptor(metaclass=ABCMeta):
    """Affords intercepting unary-unary invocations."""

    @abstractmethod
    async def intercept_unary_unary(
            self, continuation: Callable[[ClientCallDetails, RequestType],
                                         UnaryUnaryCall],
            client_call_details: ClientCallDetails,
            request: RequestType) -> Union[UnaryUnaryCall, ResponseType]:
        """Intercepts a unary-unary invocation asynchronously.

        Args:
          continuation: A coroutine that proceeds with the invocation by
            executing the next interceptor in chain or invoking the
            actual RPC on the underlying Channel. It is the interceptor's
            responsibility to call it if it decides to move the RPC forward.
            The interceptor can use
            `response_future = await continuation(client_call_details, request)`
            to continue with the RPC. `continuation` returns the response of the
            RPC.
          client_call_details: A ClientCallDetails object describing the
            outgoing RPC.
          request: The request value for the RPC.

        Returns:
          An object with the RPC response.

        Raises:
          AioRpcError: Indicating that the RPC terminated with non-OK status.
          asyncio.CancelledError: Indicating that the RPC was canceled.
        """


class InterceptedUnaryUnaryCall(_base_call.UnaryUnaryCall):
    """Used for running a `UnaryUnaryCall` wrapped by interceptors.

    Interceptors might have some work to do before the RPC invocation with
    the capacity of changing the invocation parameters, and some work to do
    after the RPC invocation with the capacity for accessing to the wrapped
    `UnaryUnaryCall`.

    It handles also early and later cancellations, when the RPC has not even
    started and the execution is still held by the interceptors or when the
    RPC has finished but again the execution is still held by the interceptors.

    Once the RPC is finally executed, all methods are finally done against the
    intercepted call, being at the same time the same call returned to the
    interceptors.

    For most of the methods, like `initial_metadata()` the caller does not need
    to wait until the interceptors task is finished, once the RPC is done the
    caller will have the freedom for accessing to the results.

    For the `__await__` method is it is proxied to the intercepted call only when
    the interceptor task is finished.
    """

    _loop: asyncio.AbstractEventLoop
    _channel: cygrpc.AioChannel
    _cancelled_before_rpc: bool
    _intercepted_call: Optional[_base_call.UnaryUnaryCall]
    _intercepted_call_created: asyncio.Event
    _interceptors_task: asyncio.Task
    _pending_add_done_callbacks: Sequence[DoneCallbackType]

    # pylint: disable=too-many-arguments
    def __init__(self, interceptors: Sequence[UnaryUnaryClientInterceptor],
                 request: RequestType, timeout: Optional[float],
                 metadata: MetadataType,
                 credentials: Optional[grpc.CallCredentials],
                 wait_for_ready: Optional[bool], channel: cygrpc.AioChannel,
                 method: bytes, request_serializer: SerializingFunction,
                 response_deserializer: DeserializingFunction,
                 loop: asyncio.AbstractEventLoop) -> None:
        self._channel = channel
        self._loop = loop
        self._interceptors_task = loop.create_task(
            self._invoke(interceptors, method, timeout, metadata, credentials,
                         wait_for_ready, request, request_serializer,
                         response_deserializer))
        self._pending_add_done_callbacks = []
        self._interceptors_task.add_done_callback(
            self._fire_pending_add_done_callbacks)

    def __del__(self):
        self.cancel()

    # pylint: disable=too-many-arguments
    async def _invoke(self, interceptors: Sequence[UnaryUnaryClientInterceptor],
                      method: bytes, timeout: Optional[float],
                      metadata: Optional[MetadataType],
                      credentials: Optional[grpc.CallCredentials],
                      wait_for_ready: Optional[bool], request: RequestType,
                      request_serializer: SerializingFunction,
                      response_deserializer: DeserializingFunction
                     ) -> UnaryUnaryCall:
        """Run the RPC call wrapped in interceptors"""

        async def _run_interceptor(
                interceptors: Iterator[UnaryUnaryClientInterceptor],
                client_call_details: ClientCallDetails,
                request: RequestType) -> _base_call.UnaryUnaryCall:

            interceptor = next(interceptors, None)

            if interceptor:
                continuation = functools.partial(_run_interceptor, interceptors)

                call_or_response = await interceptor.intercept_unary_unary(
                    continuation, client_call_details, request)

                if isinstance(call_or_response, _base_call.UnaryUnaryCall):
                    return call_or_response
                else:
                    return UnaryUnaryCallResponse(call_or_response)

            else:
                return UnaryUnaryCall(
                    request, _timeout_to_deadline(client_call_details.timeout),
                    client_call_details.metadata,
                    client_call_details.credentials,
                    client_call_details.wait_for_ready, self._channel,
                    client_call_details.method, request_serializer,
                    response_deserializer, self._loop)

        client_call_details = ClientCallDetails(method, timeout, metadata,
                                                credentials, wait_for_ready)
        return await _run_interceptor(iter(interceptors), client_call_details,
                                      request)

    def _fire_pending_add_done_callbacks(self,
                                         unused_task: asyncio.Task) -> None:
        for callback in self._pending_add_done_callbacks:
            callback(self)

        self._pending_add_done_callbacks = []

    def _wrap_add_done_callback(self, callback: DoneCallbackType,
                                unused_task: asyncio.Task) -> None:
        callback(self)

    def cancel(self) -> bool:
        if self._interceptors_task.done():
            return False

        return self._interceptors_task.cancel()

    def cancelled(self) -> bool:
        if not self._interceptors_task.done():
            return False

        try:
            call = self._interceptors_task.result()
        except AioRpcError as err:
            return err.code() == grpc.StatusCode.CANCELLED
        except asyncio.CancelledError:
            return True

        return call.cancelled()

    def done(self) -> bool:
        if not self._interceptors_task.done():
            return False

        try:
            call = self._interceptors_task.result()
        except (AioRpcError, asyncio.CancelledError):
            return True

        return call.done()

    def add_done_callback(self, callback: DoneCallbackType) -> None:
        if not self._interceptors_task.done():
            self._pending_add_done_callbacks.append(callback)
            return

        try:
            call = self._interceptors_task.result()
        except (AioRpcError, asyncio.CancelledError):
            callback(self)
            return

        if call.done():
            callback(self)
        else:
            callback = functools.partial(self._wrap_add_done_callback, callback)
            call.add_done_callback(self._wrap_add_done_callback)

    def time_remaining(self) -> Optional[float]:
        raise NotImplementedError()

    async def initial_metadata(self) -> Optional[MetadataType]:
        try:
            call = await self._interceptors_task
        except AioRpcError as err:
            return err.initial_metadata()
        except asyncio.CancelledError:
            return None

        return await call.initial_metadata()

    async def trailing_metadata(self) -> Optional[MetadataType]:
        try:
            call = await self._interceptors_task
        except AioRpcError as err:
            return err.trailing_metadata()
        except asyncio.CancelledError:
            return None

        return await call.trailing_metadata()

    async def code(self) -> grpc.StatusCode:
        try:
            call = await self._interceptors_task
        except AioRpcError as err:
            return err.code()
        except asyncio.CancelledError:
            return grpc.StatusCode.CANCELLED

        return await call.code()

    async def details(self) -> str:
        try:
            call = await self._interceptors_task
        except AioRpcError as err:
            return err.details()
        except asyncio.CancelledError:
            return _LOCAL_CANCELLATION_DETAILS

        return await call.details()

    async def debug_error_string(self) -> Optional[str]:
        try:
            call = await self._interceptors_task
        except AioRpcError as err:
            return err.debug_error_string()
        except asyncio.CancelledError:
            return ''

        return await call.debug_error_string()

    def __await__(self):
        call = yield from self._interceptors_task.__await__()
        response = yield from call.__await__()
        return response

    async def wait_for_connection(self) -> None:
        call = await self._interceptors_task
        return await call.wait_for_connection()


class UnaryUnaryCallResponse(_base_call.UnaryUnaryCall):
    """Final UnaryUnaryCall class finished with a response."""
    _response: ResponseType

    def __init__(self, response: ResponseType) -> None:
        self._response = response

    def cancel(self) -> bool:
        return False

    def cancelled(self) -> bool:
        return False

    def done(self) -> bool:
        return True

    def add_done_callback(self, unused_callback) -> None:
        raise NotImplementedError()

    def time_remaining(self) -> Optional[float]:
        raise NotImplementedError()

    async def initial_metadata(self) -> Optional[MetadataType]:
        return None

    async def trailing_metadata(self) -> Optional[MetadataType]:
        return None

    async def code(self) -> grpc.StatusCode:
        return grpc.StatusCode.OK

    async def details(self) -> str:
        return ''

    async def debug_error_string(self) -> Optional[str]:
        return None

    def __await__(self):
        if False:  # pylint: disable=using-constant-test
            # This code path is never used, but a yield statement is needed
            # for telling the interpreter that __await__ is a generator.
            yield None
        return self._response

    async def wait_for_connection(self) -> None:
        pass
