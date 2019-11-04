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
import enum
from typing import Callable, Dict, Optional, ClassVar

import grpc
from grpc import _common
from grpc._cython import cygrpc

DeserializingFunction = Callable[[bytes], str]


class AioRpcError(grpc.RpcError):
    """An RpcError to be used by the asynchronous API."""

    # TODO(https://github.com/grpc/grpc/issues/20144) Metadata
    # type returned by `initial_metadata` and `trailing_metadata`
    # and also taken in the constructor needs to be revisit and make
    # it more specific.

    _code: grpc.StatusCode
    _details: Optional[str]
    _initial_metadata: Optional[Dict]
    _trailing_metadata: Optional[Dict]

    def __init__(self,
                 code: grpc.StatusCode,
                 details: Optional[str] = None,
                 initial_metadata: Optional[Dict] = None,
                 trailing_metadata: Optional[Dict] = None):
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

    def code(self) -> grpc.StatusCode:
        """
        Returns:
          The `grpc.StatusCode` status code.
        """
        return self._code

    def details(self) -> Optional[str]:
        """
        Returns:
          The description of the error.
        """
        return self._details

    def initial_metadata(self) -> Optional[Dict]:
        """
        Returns:
          The inital metadata received.
        """
        return self._initial_metadata

    def trailing_metadata(self) -> Optional[Dict]:
        """
        Returns:
          The trailing metadata received.
        """
        return self._trailing_metadata


@enum.unique
class _RpcState(enum.Enum):
    """Identifies the state of the RPC."""
    ONGOING = 1
    CANCELLED = 2
    FINISHED = 3
    ABORT = 4


class Call:
    """Object for managing RPC calls,
    returned when an instance of `UnaryUnaryMultiCallable` object is called.
    """

    _cancellation_details: ClassVar[str] = 'Locally cancelled by application!'

    _state: _RpcState
    _exception: Optional[Exception]
    _response: Optional[bytes]
    _code: grpc.StatusCode
    _details: Optional[str]
    _initial_metadata: Optional[Dict]
    _trailing_metadata: Optional[Dict]
    _call: asyncio.Task
    _call_cancel_status: cygrpc.AioCancelStatus
    _response_deserializer: DeserializingFunction

    def __init__(self, call: asyncio.Task,
                 response_deserializer: DeserializingFunction,
                 call_cancel_status: cygrpc.AioCancelStatus) -> None:
        """Constructor.

        Args:
          call: Asyncio Task that holds the RPC execution.
          response_deserializer: Deserializer used for parsing the reponse.
          call_cancel_status: A cygrpc.AioCancelStatus used for giving a
            specific error when the RPC is canceled.
        """

        self._state = _RpcState.ONGOING
        self._exception = None
        self._response = None
        self._code = grpc.StatusCode.UNKNOWN
        self._details = None
        self._initial_metadata = None
        self._trailing_metadata = None
        self._call = call
        self._call_cancel_status = call_cancel_status
        self._response_deserializer = response_deserializer

    def __del__(self):
        self.cancel()

    def cancel(self) -> bool:
        """Cancels the ongoing RPC request.

        Returns:
          True if the RPC can be canceled, False if was already cancelled or terminated.
        """
        if self.cancelled() or self.done():
            return False

        code = grpc.StatusCode.CANCELLED
        self._call_cancel_status.cancel(
            _common.STATUS_CODE_TO_CYGRPC_STATUS_CODE[code],
            details=Call._cancellation_details)
        self._call.cancel()
        self._details = Call._cancellation_details
        self._code = code
        self._state = _RpcState.CANCELLED
        return True

    def cancelled(self) -> bool:
        """Returns if the RPC was cancelled.

        Returns:
          True if the requests was cancelled, False if not.
        """
        return self._state is _RpcState.CANCELLED

    def running(self) -> bool:
        """Returns if the RPC is running.

        Returns:
          True if the requests is running, False if it already terminated.
        """
        return not self.done()

    def done(self) -> bool:
        """Returns if the RPC has finished.

        Returns:
          True if the requests has finished, False is if still ongoing.
        """
        return self._state is not _RpcState.ONGOING

    async def initial_metadata(self):
        raise NotImplementedError()

    async def trailing_metadata(self):
        raise NotImplementedError()

    async def code(self) -> grpc.StatusCode:
        """Returns the `grpc.StatusCode` if the RPC is finished,
        otherwise first waits until the RPC finishes.

        Returns:
          The `grpc.StatusCode` status code.
        """
        if not self.done():
            try:
                await self
            except (asyncio.CancelledError, AioRpcError):
                pass

        return self._code

    async def details(self) -> str:
        """Returns the details if the RPC is finished, otherwise first waits till the
        RPC finishes.

        Returns:
          The details.
        """
        if not self.done():
            try:
                await self
            except (asyncio.CancelledError, AioRpcError):
                pass

        return self._details

    def __await__(self):
        """Wait till the ongoing RPC request finishes.

        Returns:
          Response of the RPC call.

        Raises:
          AioRpcError: Indicating that the RPC terminated with non-OK status.
          asyncio.CancelledError: Indicating that the RPC was canceled.
        """
        # We can not relay on the `done()` method since some exceptions
        # might be pending to be catched, like `asyncio.CancelledError`.
        if self._response:
            return self._response
        elif self._exception:
            raise self._exception

        try:
            buffer_ = yield from self._call.__await__()
        except cygrpc.AioRpcError as aio_rpc_error:
            self._state = _RpcState.ABORT
            self._code = _common.CYGRPC_STATUS_CODE_TO_STATUS_CODE[
                aio_rpc_error.code()]
            self._details = aio_rpc_error.details()
            self._initial_metadata = aio_rpc_error.initial_metadata()
            self._trailing_metadata = aio_rpc_error.trailing_metadata()

            # Propagates the pure Python class
            self._exception = AioRpcError(self._code, self._details,
                                          self._initial_metadata,
                                          self._trailing_metadata)
            raise self._exception from aio_rpc_error
        except asyncio.CancelledError as cancel_error:
            # _state, _code, _details are managed in the `cancel` method
            self._exception = cancel_error
            raise

        self._response = _common.deserialize(buffer_,
                                             self._response_deserializer)
        self._code = grpc.StatusCode.OK
        self._state = _RpcState.FINISHED
        return self._response
