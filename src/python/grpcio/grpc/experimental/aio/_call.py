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
from typing import AsyncIterable, Awaitable, Dict, Optional

import grpc
from grpc import _common
from grpc._cython import cygrpc

from . import _base_call
from ._typing import (DeserializingFunction, MetadataType, RequestType,
                      ResponseType, SerializingFunction)

__all__ = 'AioRpcError', 'Call', 'UnaryUnaryCall', 'UnaryStreamCall'

_LOCAL_CANCELLATION_DETAILS = 'Locally cancelled by application!'
_GC_CANCELLATION_DETAILS = 'Cancelled upon garbage collection!'
_RPC_ALREADY_FINISHED_DETAILS = 'RPC already finished.'

_OK_CALL_REPRESENTATION = ('<{} of RPC that terminated with:\n'
                           '\tstatus = {}\n'
                           '\tdetails = "{}"\n'
                           '>')

_NON_OK_CALL_REPRESENTATION = ('<{} of RPC that terminated with:\n'
                               '\tstatus = {}\n'
                               '\tdetails = "{}"\n'
                               '\tdebug_error_string = "{}"\n'
                               '>')

_EMPTY_METADATA = tuple()


class AioRpcError(grpc.RpcError):
    """An implementation of RpcError to be used by the asynchronous API.

    Raised RpcError is a snapshot of the final status of the RPC, values are
    determined. Hence, its methods no longer needs to be coroutines.
    """

    # TODO(https://github.com/grpc/grpc/issues/20144) Metadata
    # type returned by `initial_metadata` and `trailing_metadata`
    # and also taken in the constructor needs to be revisit and make
    # it more specific.

    _code: grpc.StatusCode
    _details: Optional[str]
    _initial_metadata: Optional[MetadataType]
    _trailing_metadata: Optional[MetadataType]
    _debug_error_string: Optional[str]

    def __init__(self,
                 code: grpc.StatusCode,
                 details: Optional[str] = None,
                 initial_metadata: Optional[MetadataType] = None,
                 trailing_metadata: Optional[MetadataType] = None,
                 debug_error_string: Optional[str] = None) -> None:
        """Constructor.

        Args:
          code: The status code with which the RPC has been finalized.
          details: Optional details explaining the reason of the error.
          initial_metadata: Optional initial metadata that could be sent by the
            Server.
          trailing_metadata: Optional metadata that could be sent by the Server.
        """

        super().__init__(self)
        self._code = code
        self._details = details
        self._initial_metadata = initial_metadata
        self._trailing_metadata = trailing_metadata
        self._debug_error_string = debug_error_string

    def code(self) -> grpc.StatusCode:
        """Accesses the status code sent by the server.

        Returns:
          The `grpc.StatusCode` status code.
        """
        return self._code

    def details(self) -> Optional[str]:
        """Accesses the details sent by the server.

        Returns:
          The description of the error.
        """
        return self._details

    def initial_metadata(self) -> Optional[Dict]:
        """Accesses the initial metadata sent by the server.

        Returns:
          The initial metadata received.
        """
        return self._initial_metadata

    def trailing_metadata(self) -> Optional[Dict]:
        """Accesses the trailing metadata sent by the server.

        Returns:
          The trailing metadata received.
        """
        return self._trailing_metadata

    def debug_error_string(self) -> str:
        """Accesses the debug error string sent by the server.

        Returns:
          The debug error string received.
        """
        return self._debug_error_string

    def _repr(self) -> str:
        """Assembles the error string for the RPC error."""
        return _NON_OK_CALL_REPRESENTATION.format(self.__class__.__name__,
                                                  self._code, self._details,
                                                  self._debug_error_string)

    def __repr__(self) -> str:
        return self._repr()

    def __str__(self) -> str:
        return self._repr()


def _create_rpc_error(initial_metadata: Optional[MetadataType],
                      status: cygrpc.AioRpcStatus) -> AioRpcError:
    return AioRpcError(_common.CYGRPC_STATUS_CODE_TO_STATUS_CODE[status.code()],
                       status.details(), initial_metadata,
                       status.trailing_metadata())


class Call(_base_call.Call):
    _loop: asyncio.AbstractEventLoop
    _code: grpc.StatusCode
    _status: Awaitable[cygrpc.AioRpcStatus]
    _initial_metadata: Awaitable[MetadataType]
    _locally_cancelled: bool

    def __init__(self) -> None:
        self._loop = asyncio.get_event_loop()
        self._code = None
        self._status = self._loop.create_future()
        self._initial_metadata = self._loop.create_future()
        self._locally_cancelled = False

    def cancel(self) -> bool:
        """Placeholder cancellation method.

        The implementation of this method needs to pass the cancellation reason
        into self._cancellation, using `set_result` instead of
        `set_exception`.
        """
        raise NotImplementedError()

    def cancelled(self) -> bool:
        return self._code == grpc.StatusCode.CANCELLED

    def done(self) -> bool:
        return self._status.done()

    def add_done_callback(self, unused_callback) -> None:
        raise NotImplementedError()

    def time_remaining(self) -> Optional[float]:
        raise NotImplementedError()

    async def initial_metadata(self) -> MetadataType:
        return await self._initial_metadata

    async def trailing_metadata(self) -> MetadataType:
        return (await self._status).trailing_metadata()

    async def code(self) -> grpc.StatusCode:
        await self._status
        return self._code

    async def details(self) -> str:
        return (await self._status).details()

    async def debug_error_string(self) -> str:
        return (await self._status).debug_error_string()

    def _set_initial_metadata(self, metadata: MetadataType) -> None:
        self._initial_metadata.set_result(metadata)

    def _set_status(self, status: cygrpc.AioRpcStatus) -> None:
        """Private method to set final status of the RPC.

        This method may be called multiple time due to data race between local
        cancellation (by application) and Core receiving status from peer. We
        make no promise here which one will win.
        """
        # In case of local cancellation, flip the flag.
        if status.details() is _LOCAL_CANCELLATION_DETAILS:
            self._locally_cancelled = True

        # In case of the RPC finished without receiving metadata.
        if not self._initial_metadata.done():
            self._initial_metadata.set_result(_EMPTY_METADATA)

        # Sets final status
        self._status.set_result(status)
        self._code = _common.CYGRPC_STATUS_CODE_TO_STATUS_CODE[status.code()]

    async def _raise_for_status(self) -> None:
        if self._locally_cancelled:
            raise asyncio.CancelledError()
        await self._status
        if self._code != grpc.StatusCode.OK:
            raise _create_rpc_error(await self.initial_metadata(),
                                    self._status.result())

    def _repr(self) -> str:
        """Assembles the RPC representation string."""
        if not self._status.done():
            return '<{} object>'.format(self.__class__.__name__)
        if self._code is grpc.StatusCode.OK:
            return _OK_CALL_REPRESENTATION.format(
                self.__class__.__name__, self._code,
                self._status.result().details())
        else:
            return _NON_OK_CALL_REPRESENTATION.format(
                self.__class__.__name__, self._code,
                self._status.result().details(),
                self._status.result().debug_error_string())

    def __repr__(self) -> str:
        return self._repr()

    def __str__(self) -> str:
        return self._repr()


# pylint: disable=abstract-method
class UnaryUnaryCall(Call, _base_call.UnaryUnaryCall):
    """Object for managing unary-unary RPC calls.

    Returned when an instance of `UnaryUnaryMultiCallable` object is called.
    """
    _request: RequestType
    _channel: cygrpc.AioChannel
    _request_serializer: SerializingFunction
    _response_deserializer: DeserializingFunction
    _call: asyncio.Task
    _cython_call: cygrpc._AioCall

    def __init__(self, request: RequestType, deadline: Optional[float],
                 channel: cygrpc.AioChannel, method: bytes,
                 request_serializer: SerializingFunction,
                 response_deserializer: DeserializingFunction) -> None:
        super().__init__()
        self._request = request
        self._channel = channel
        self._request_serializer = request_serializer
        self._response_deserializer = response_deserializer
        self._cython_call = self._channel.call(method, deadline)
        self._call = self._loop.create_task(self._invoke())

    def __del__(self) -> None:
        if not self._call.done():
            self._cancel(
                cygrpc.AioRpcStatus(cygrpc.StatusCode.cancelled,
                                    _GC_CANCELLATION_DETAILS, None, None))

    async def _invoke(self) -> ResponseType:
        serialized_request = _common.serialize(self._request,
                                               self._request_serializer)

        # NOTE(lidiz) asyncio.CancelledError is not a good transport for status,
        # because the asyncio.Task class do not cache the exception object.
        # https://github.com/python/cpython/blob/edad4d89e357c92f70c0324b937845d652b20afd/Lib/asyncio/tasks.py#L785
        try:
            serialized_response = await self._cython_call.unary_unary(
                serialized_request,
                self._set_initial_metadata,
                self._set_status,
            )
        except asyncio.CancelledError:
            if self._code != grpc.StatusCode.CANCELLED:
                self.cancel()

        # Raises here if RPC failed or cancelled
        await self._raise_for_status()

        return _common.deserialize(serialized_response,
                                   self._response_deserializer)

    def _cancel(self, status: cygrpc.AioRpcStatus) -> bool:
        """Forwards the application cancellation reasoning."""
        if not self._status.done():
            self._set_status(status)
            self._cython_call.cancel(status)
            self._call.cancel()
            return True
        else:
            return False

    def cancel(self) -> bool:
        return self._cancel(
            cygrpc.AioRpcStatus(cygrpc.StatusCode.cancelled,
                                _LOCAL_CANCELLATION_DETAILS, None, None))

    def __await__(self) -> ResponseType:
        """Wait till the ongoing RPC request finishes."""
        try:
            response = yield from self._call
        except asyncio.CancelledError:
            # Even if we caught all other CancelledError, there is still
            # this corner case. If the application cancels immediately after
            # the Call object is created, we will observe this
            # `CancelledError`.
            if not self.cancelled():
                self.cancel()
            raise
        return response


# pylint: disable=abstract-method
class UnaryStreamCall(Call, _base_call.UnaryStreamCall):
    """Object for managing unary-stream RPC calls.

    Returned when an instance of `UnaryStreamMultiCallable` object is called.
    """
    _request: RequestType
    _channel: cygrpc.AioChannel
    _request_serializer: SerializingFunction
    _response_deserializer: DeserializingFunction
    _cython_call: cygrpc._AioCall
    _send_unary_request_task: asyncio.Task
    _message_aiter: AsyncIterable[ResponseType]

    def __init__(self, request: RequestType, deadline: Optional[float],
                 channel: cygrpc.AioChannel, method: bytes,
                 request_serializer: SerializingFunction,
                 response_deserializer: DeserializingFunction) -> None:
        super().__init__()
        self._request = request
        self._channel = channel
        self._request_serializer = request_serializer
        self._response_deserializer = response_deserializer
        self._send_unary_request_task = self._loop.create_task(
            self._send_unary_request())
        self._message_aiter = self._fetch_stream_responses()
        self._cython_call = self._channel.call(method, deadline)

    def __del__(self) -> None:
        if not self._status.done():
            self._cancel(
                cygrpc.AioRpcStatus(cygrpc.StatusCode.cancelled,
                                    _GC_CANCELLATION_DETAILS, None, None))

    async def _send_unary_request(self) -> ResponseType:
        serialized_request = _common.serialize(self._request,
                                               self._request_serializer)
        try:
            await self._cython_call.unary_stream(serialized_request,
                                                 self._set_initial_metadata,
                                                 self._set_status)
        except asyncio.CancelledError:
            if self._code != grpc.StatusCode.CANCELLED:
                self.cancel()
            raise

    async def _fetch_stream_responses(self) -> ResponseType:
        await self._send_unary_request_task
        message = await self._read()
        while message:
            yield message
            message = await self._read()

    def _cancel(self, status: cygrpc.AioRpcStatus) -> bool:
        """Forwards the application cancellation reasoning.

        Async generator will receive an exception. The cancellation will go
        deep down into Core, and then propagates backup as the
        `cygrpc.AioRpcStatus` exception.

        So, under race condition, e.g. the server sent out final state headers
        and the client calling "cancel" at the same time, this method respects
        the winner in Core.
        """
        if not self._status.done():
            self._set_status(status)
            self._cython_call.cancel(status)

            if not self._send_unary_request_task.done():
                # Injects CancelledError to the Task. The exception will
                # propagate to _fetch_stream_responses as well, if the sending
                # is not done.
                self._send_unary_request_task.cancel()
            return True
        else:
            return False

    def cancel(self) -> bool:
        return self._cancel(
            cygrpc.AioRpcStatus(cygrpc.StatusCode.cancelled,
                                _LOCAL_CANCELLATION_DETAILS, None, None))

    def __aiter__(self) -> AsyncIterable[ResponseType]:
        return self._message_aiter

    async def _read(self) -> ResponseType:
        # Wait for the request being sent
        await self._send_unary_request_task

        # Reads response message from Core
        try:
            raw_response = await self._cython_call.receive_serialized_message()
        except asyncio.CancelledError:
            if self._code != grpc.StatusCode.CANCELLED:
                self.cancel()
            raise

        if raw_response is None:
            return None
        else:
            return _common.deserialize(raw_response,
                                       self._response_deserializer)

    async def read(self) -> ResponseType:
        if self._status.done():
            await self._raise_for_status()
            raise asyncio.InvalidStateError(_RPC_ALREADY_FINISHED_DETAILS)

        response_message = await self._read()

        if response_message is None:
            # If the read operation failed, Core should explain why.
            await self._raise_for_status()
            # If no exception raised, there is something wrong internally.
            assert False, 'Read operation failed with StatusCode.OK'
        else:
            return response_message
