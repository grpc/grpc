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


_EMPTY_FLAGS = 0
_EMPTY_MASK = 0
_IMMUTABLE_EMPTY_METADATA = tuple()

_UNKNOWN_CANCELLATION_DETAILS = 'RPC cancelled for unknown reason.'
_OK_CALL_REPRESENTATION = ('<{} of RPC that terminated with:\n'
                           '\tstatus = {}\n'
                           '\tdetails = "{}"\n'
                           '>')

_NON_OK_CALL_REPRESENTATION = ('<{} of RPC that terminated with:\n'
                               '\tstatus = {}\n'
                               '\tdetails = "{}"\n'
                               '\tdebug_error_string = "{}"\n'
                               '>')


cdef int _get_send_initial_metadata_flags(object wait_for_ready) except *:
    cdef int flags = 0
    # Wait-for-ready can be None, which means using default value in Core.
    if wait_for_ready is not None:
        flags |= InitialMetadataFlags.wait_for_ready_explicitly_set
        if wait_for_ready:
            flags |= InitialMetadataFlags.wait_for_ready

    flags &= InitialMetadataFlags.used_mask
    return flags


cdef class _AioCall(GrpcCallWrapper):

    def __cinit__(self, AioChannel channel, object deadline,
                  bytes method, CallCredentials call_credentials, object wait_for_ready):
        init_grpc_aio()
        self.call = NULL
        self._channel = channel
        self._loop = channel.loop
        self._references = []
        self._status = None
        self._initial_metadata = None
        self._waiters_status = []
        self._waiters_initial_metadata = []
        self._done_callbacks = []
        self._is_locally_cancelled = False
        self._deadline = deadline
        self._send_initial_metadata_flags = _get_send_initial_metadata_flags(wait_for_ready)
        self._create_grpc_call(deadline, method, call_credentials)

    def __dealloc__(self):
        if self.call:
            grpc_call_unref(self.call)
        shutdown_grpc_aio()

    def _repr(self) -> str:
        """Assembles the RPC representation string."""
        # This needs to be loaded at run time once everything
        # has been loaded.
        from grpc import _common

        if not self.done():
            return '<{} object>'.format(self.__class__.__name__)

        if self._status.code() is StatusCode.ok:
            return _OK_CALL_REPRESENTATION.format(
                self.__class__.__name__,
                _common.CYGRPC_STATUS_CODE_TO_STATUS_CODE[self._status.code()],
                self._status.details())
        else:
            return _NON_OK_CALL_REPRESENTATION.format(
                self.__class__.__name__,
                self._status.details(),
                _common.CYGRPC_STATUS_CODE_TO_STATUS_CODE[self._status.code()],
                self._status.debug_error_string())

    def __repr__(self) -> str:
        return self._repr()

    def __str__(self) -> str:
        return self._repr()

    cdef void _create_grpc_call(self,
                                object deadline,
                                bytes method,
                                CallCredentials credentials) except *:
        """Creates the corresponding Core object for this RPC.

        For unary calls, the grpc_call lives shortly and can be destroyed after
        invoke start_batch. However, if either side is streaming, the grpc_call
        life span will be longer than one function. So, it would better save it
        as an instance variable than a stack variable, which reflects its
        nature in Core.
        """
        cdef grpc_slice method_slice
        cdef gpr_timespec c_deadline = _timespec_from_time(deadline)
        cdef grpc_call_error set_credentials_error

        method_slice = grpc_slice_from_copied_buffer(
            <const char *> method,
            <size_t> len(method)
        )
        self.call = grpc_channel_create_call(
            self._channel.channel,
            NULL,
            _EMPTY_MASK,
            global_completion_queue(),
            method_slice,
            NULL,
            c_deadline,
            NULL
        )

        if credentials is not None:
            set_credentials_error = grpc_call_set_credentials(self.call, credentials.c())
            if set_credentials_error != GRPC_CALL_OK:
                raise InternalError("Credentials couldn't have been set: {0}".format(set_credentials_error))

        grpc_slice_unref(method_slice)

    cdef void _set_status(self, AioRpcStatus status) except *:
        cdef list waiters

        # No more waiters should be expected since status has been set.
        self._status = status

        if self._initial_metadata is None:
            self._set_initial_metadata(_IMMUTABLE_EMPTY_METADATA)

        for waiter in self._waiters_status:
            if not waiter.done():
                waiter.set_result(None)
        self._waiters_status = []

        for callback in self._done_callbacks:
            callback()

    cdef void _set_initial_metadata(self, tuple initial_metadata) except *:
        if self._initial_metadata is not None:
            # Some gRPC calls might end before the initial metadata arrived in
            # the Call object. That causes this method to be invoked twice: 1.
            # filled with an empty metadata; 2. updated with the actual user
            # provided metadata.
            return

        cdef list waiters

        # No more waiters should be expected since initial metadata has been
        # set.
        self._initial_metadata = initial_metadata

        for waiter in self._waiters_initial_metadata:
            if not waiter.done():
                waiter.set_result(None)
        self._waiters_initial_metadata = []

    def add_done_callback(self, callback):
        if self.done():
            callback()
        else:
            self._done_callbacks.append(callback)

    def time_remaining(self):
        if self._deadline is None:
            return None
        else:
            return max(0, self._deadline - time.time())

    def cancel(self, str details):
        """Cancels the RPC in Core with given RPC status.

        Above abstractions must invoke this method to set Core objects into
        proper state.
        """
        self._is_locally_cancelled = True

        cdef object details_bytes
        cdef char *c_details
        cdef grpc_call_error error

        self._set_status(AioRpcStatus(
            StatusCode.cancelled,
            details,
            None,
            None,
        ))

        details_bytes = str_to_bytes(details)
        self._references.append(details_bytes)
        c_details = <char *>details_bytes
        # By implementation, grpc_call_cancel_with_status always return OK
        error = grpc_call_cancel_with_status(
            self.call,
            StatusCode.cancelled,
            c_details,
            NULL,
        )
        assert error == GRPC_CALL_OK

    def done(self):
        """Returns if the RPC call has finished.

        Checks if the status has been provided, either
        because the RPC finished or because was cancelled..

        Returns:
            True if the RPC can be considered finished.
        """
        return self._status is not None

    def cancelled(self):
        """Returns if the RPC was cancelled.

        Returns:
            True if the RPC was cancelled.
        """
        if not self.done():
            return False

        return self._status.code() == StatusCode.cancelled

    async def status(self):
        """Returns the status of the RPC call.

        It returns the finshed status of the RPC. If the RPC
        has not finished yet this function will wait until the RPC
        gets finished.

        Returns:
            Finished status of the RPC as an AioRpcStatus object.
        """
        if self._status is not None:
            return self._status

        future = self._loop.create_future()
        self._waiters_status.append(future)
        await future

        return self._status

    def is_ok(self):
        """Returns if the RPC is ended with ok."""
        return self.done() and self._status.code() == StatusCode.ok

    async def initial_metadata(self):
        """Returns the initial metadata of the RPC call.

        If the initial metadata has not been received yet this function will
        wait until the RPC gets finished.

        Returns:
            The tuple object with the initial metadata.
        """
        if self._initial_metadata is not None:
            return self._initial_metadata

        future = self._loop.create_future()
        self._waiters_initial_metadata.append(future)
        await future

        return self._initial_metadata

    def is_locally_cancelled(self):
        """Returns if the RPC was cancelled locally.

        Returns:
            True when was cancelled locally, False when was cancelled remotelly or
            is still ongoing.
        """
        if self._is_locally_cancelled:
            return True

        return False

    def set_internal_error(self, str error_str):
        self._set_status(AioRpcStatus(
            StatusCode.internal,
            'Internal error from Core',
            (),
            error_str,
        ))

    async def unary_unary(self,
                          bytes request,
                          tuple outbound_initial_metadata):
        """Performs a unary unary RPC.

        Args:
          request: the serialized requests in bytes.
          outbound_initial_metadata: optional outbound metadata.
        """
        cdef tuple ops

        cdef SendInitialMetadataOperation initial_metadata_op = SendInitialMetadataOperation(
            outbound_initial_metadata,
            self._send_initial_metadata_flags)
        cdef SendMessageOperation send_message_op = SendMessageOperation(request, _EMPTY_FLAGS)
        cdef SendCloseFromClientOperation send_close_op = SendCloseFromClientOperation(_EMPTY_FLAGS)
        cdef ReceiveInitialMetadataOperation receive_initial_metadata_op = ReceiveInitialMetadataOperation(_EMPTY_FLAGS)
        cdef ReceiveMessageOperation receive_message_op = ReceiveMessageOperation(_EMPTY_FLAGS)
        cdef ReceiveStatusOnClientOperation receive_status_on_client_op = ReceiveStatusOnClientOperation(_EMPTY_FLAGS)

        ops = (initial_metadata_op, send_message_op, send_close_op,
               receive_initial_metadata_op, receive_message_op,
               receive_status_on_client_op)

        # Executes all operations in one batch.
        # Might raise CancelledError, handling it in Python UnaryUnaryCall.
        await execute_batch(self,
                            ops,
                            self._loop)

        self._set_initial_metadata(receive_initial_metadata_op.initial_metadata())

        cdef grpc_status_code code
        code = receive_status_on_client_op.code()

        self._set_status(AioRpcStatus(
            code,
            receive_status_on_client_op.details(),
            receive_status_on_client_op.trailing_metadata(),
            receive_status_on_client_op.error_string(),
        ))

        if code == StatusCode.ok:
            return receive_message_op.message()
        else:
            return None

    async def _handle_status_once_received(self):
        """Handles the status sent by peer once received."""
        cdef ReceiveStatusOnClientOperation op = ReceiveStatusOnClientOperation(_EMPTY_FLAGS)
        cdef tuple ops = (op,)
        await execute_batch(self, ops, self._loop)

        # Halts if the RPC is locally cancelled
        if self._is_locally_cancelled:
            return

        self._set_status(AioRpcStatus(
            op.code(),
            op.details(),
            op.trailing_metadata(),
            op.error_string(),
        ))

    async def receive_serialized_message(self):
        """Receives one single raw message in bytes."""
        cdef bytes received_message

        # Receives a message. Returns None when failed:
        # * EOF, no more messages to read;
        # * The client application cancels;
        # * The server sends final status.
        received_message = await _receive_message(
            self,
            self._loop
        )
        if received_message is not None:
            return received_message
        else:
            return EOF

    async def send_serialized_message(self, bytes message):
        """Sends one single raw message in bytes."""
        await _send_message(self,
                            message,
                            None,
                            False,
                            self._loop)

    async def send_receive_close(self):
        """Half close the RPC on the client-side."""
        cdef SendCloseFromClientOperation op = SendCloseFromClientOperation(_EMPTY_FLAGS)
        cdef tuple ops = (op,)
        await execute_batch(self, ops, self._loop)

    async def initiate_unary_stream(self,
                           bytes request,
                           tuple outbound_initial_metadata):
        """Implementation of the start of a unary-stream call."""
        # Peer may prematurely end this RPC at any point. We need a corutine
        # that watches if the server sends the final status.
        status_task = self._loop.create_task(self._handle_status_once_received())

        cdef tuple outbound_ops
        cdef Operation initial_metadata_op = SendInitialMetadataOperation(
            outbound_initial_metadata,
            self._send_initial_metadata_flags)
        cdef Operation send_message_op = SendMessageOperation(
            request,
            _EMPTY_FLAGS)
        cdef Operation send_close_op = SendCloseFromClientOperation(
            _EMPTY_FLAGS)

        outbound_ops = (
            initial_metadata_op,
            send_message_op,
            send_close_op,
        )

        try:
            # Sends out the request message.
            await execute_batch(self,
                                outbound_ops,
                                self._loop)

            # Receives initial metadata.
            self._set_initial_metadata(
                await _receive_initial_metadata(self,
                                                self._loop),
            )
        except ExecuteBatchError as batch_error:
            # Core should explain why this batch failed
            await status_task

    async def stream_unary(self,
                           tuple outbound_initial_metadata,
                           object metadata_sent_observer):
        """Actual implementation of the complete unary-stream call.

        Needs to pay extra attention to the raise mechanism. If we want to
        propagate the final status exception, then we have to raise it.
        Othersize, it would end normally and raise `StopAsyncIteration()`.
        """
        try:
            # Sends out initial_metadata ASAP.
            await _send_initial_metadata(self,
                                        outbound_initial_metadata,
                                        self._send_initial_metadata_flags,
                                        self._loop)
            # Notify upper level that sending messages are allowed now.
            metadata_sent_observer()

            # Receives initial metadata.
            self._set_initial_metadata(
                await _receive_initial_metadata(self, self._loop)
            )
        except ExecuteBatchError:
            # Core should explain why this batch failed
            await self._handle_status_once_received()

            # Allow upper layer to proceed only if the status is set
            metadata_sent_observer()
            return None

        cdef tuple inbound_ops
        cdef ReceiveMessageOperation receive_message_op = ReceiveMessageOperation(_EMPTY_FLAGS)
        cdef ReceiveStatusOnClientOperation receive_status_on_client_op = ReceiveStatusOnClientOperation(_EMPTY_FLAGS)
        inbound_ops = (receive_message_op, receive_status_on_client_op)

        # Executes all operations in one batch.
        await execute_batch(self,
                            inbound_ops,
                            self._loop)

        cdef grpc_status_code code
        code = receive_status_on_client_op.code()

        self._set_status(AioRpcStatus(
            code,
            receive_status_on_client_op.details(),
            receive_status_on_client_op.trailing_metadata(),
            receive_status_on_client_op.error_string(),
        ))

        if code == StatusCode.ok:
            return receive_message_op.message()
        else:
            return None

    async def initiate_stream_stream(self,
                           tuple outbound_initial_metadata,
                           object metadata_sent_observer):
        """Actual implementation of the complete stream-stream call.

        Needs to pay extra attention to the raise mechanism. If we want to
        propagate the final status exception, then we have to raise it.
        Othersize, it would end normally and raise `StopAsyncIteration()`.
        """
        # Peer may prematurely end this RPC at any point. We need a corutine
        # that watches if the server sends the final status.
        status_task = self._loop.create_task(self._handle_status_once_received())

        try:
            # Sends out initial_metadata ASAP.
            await _send_initial_metadata(self,
                                        outbound_initial_metadata,
                                        self._send_initial_metadata_flags,
                                        self._loop)
            # Notify upper level that sending messages are allowed now.
            metadata_sent_observer()

            # Receives initial metadata.
            self._set_initial_metadata(
                await _receive_initial_metadata(self, self._loop)
            )
        except ExecuteBatchError as batch_error:
            # Core should explain why this batch failed
            await status_task

            # Allow upper layer to proceed only if the status is set
            metadata_sent_observer()