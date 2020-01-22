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

cimport cpython
import grpc


_EMPTY_FLAGS = 0
_EMPTY_MASK = 0
_EMPTY_METADATA = None

_UNKNOWN_CANCELLATION_DETAILS = 'RPC cancelled for unknown reason.'


cdef class _AioCall(GrpcCallWrapper):

    def __cinit__(self,
                  AioChannel channel,
                  object deadline,
                  bytes method,
                  CallCredentials call_credentials):
        self.call = NULL
        self._channel = channel
        self._references = []
        self._loop = asyncio.get_event_loop()
        self._create_grpc_call(deadline, method, call_credentials)
        self._is_locally_cancelled = False
        self._deadline = deadline

    def __dealloc__(self):
        if self.call:
            grpc_call_unref(self.call)

    def __repr__(self):
        class_name = self.__class__.__name__
        id_ = id(self)
        return f"<{class_name} {id_}>"

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
            self._channel.cq.c_ptr(),
            method_slice,
            NULL,
            c_deadline,
            NULL
        )

        if credentials is not None:
            set_credentials_error = grpc_call_set_credentials(self.call, credentials.c())
            if set_credentials_error != GRPC_CALL_OK:
                raise Exception("Credentials couldn't have been set")

        grpc_slice_unref(method_slice)

    def time_remaining(self):
        if self._deadline is None:
            return None
        else:
            return max(0, self._deadline - time.time())

    def cancel(self, AioRpcStatus status):
        """Cancels the RPC in Core with given RPC status.
        
        Above abstractions must invoke this method to set Core objects into
        proper state.
        """
        self._is_locally_cancelled = True

        cdef object details
        cdef char *c_details
        cdef grpc_call_error error
        # Try to fetch application layer cancellation details in the future.
        # * If cancellation details present, cancel with status;
        # * If details not present, cancel with unknown reason.
        if status is not None:
            details = str_to_bytes(status.details())
            self._references.append(details)
            c_details = <char *>details
            # By implementation, grpc_call_cancel_with_status always return OK
            error = grpc_call_cancel_with_status(
                self.call,
                status.c_code(),
                c_details,
                NULL,
            )
            assert error == GRPC_CALL_OK
        else:
            # By implementation, grpc_call_cancel always return OK
            error = grpc_call_cancel(self.call, NULL)
            assert error == GRPC_CALL_OK

    async def unary_unary(self,
                          bytes request,
                          tuple outbound_initial_metadata,
                          object initial_metadata_observer,
                          object status_observer):
        """Performs a unary unary RPC.
        
        Args:
          method: name of the calling method in bytes.
          request: the serialized requests in bytes.
          deadline: optional deadline of the RPC in float.
          cancellation_future: the future that meant to transport the
            cancellation reason from the application layer.
          initial_metadata_observer: a callback for received initial metadata.
          status_observer: a callback for received final status.
        """
        cdef tuple ops

        cdef SendInitialMetadataOperation initial_metadata_op = SendInitialMetadataOperation(
            outbound_initial_metadata,
            GRPC_INITIAL_METADATA_USED_MASK)
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

        # Reports received initial metadata.
        initial_metadata_observer(receive_initial_metadata_op.initial_metadata())

        status = AioRpcStatus(
            receive_status_on_client_op.code(),
            receive_status_on_client_op.details(),
            receive_status_on_client_op.trailing_metadata(),
            receive_status_on_client_op.error_string(),
        )
        # Reports the final status of the RPC to Python layer. The observer
        # pattern is used here to unify unary and streaming code path.
        status_observer(status)

        if status.code() == StatusCode.ok:
            return receive_message_op.message()
        else:
            return None

    async def _handle_status_once_received(self, object status_observer):
        """Handles the status sent by peer once received."""
        cdef ReceiveStatusOnClientOperation op = ReceiveStatusOnClientOperation(_EMPTY_FLAGS)
        cdef tuple ops = (op,)
        await execute_batch(self, ops, self._loop)

        # Halts if the RPC is locally cancelled
        if self._is_locally_cancelled:
            return

        cdef AioRpcStatus status = AioRpcStatus(
            op.code(),
            op.details(),
            op.trailing_metadata(),
            op.error_string(),
        )
        status_observer(status)

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
        if received_message:
            return received_message
        else:
            return EOF

    async def send_serialized_message(self, bytes message):
        """Sends one single raw message in bytes."""
        await _send_message(self,
                            message,
                            True,
                            self._loop)

    async def send_receive_close(self):
        """Half close the RPC on the client-side."""
        cdef SendCloseFromClientOperation op = SendCloseFromClientOperation(_EMPTY_FLAGS)
        cdef tuple ops = (op,)
        await execute_batch(self, ops, self._loop)

    async def initiate_unary_stream(self,
                           bytes request,
                           tuple outbound_initial_metadata,
                           object initial_metadata_observer,
                           object status_observer):
        """Implementation of the start of a unary-stream call."""
        # Peer may prematurely end this RPC at any point. We need a corutine
        # that watches if the server sends the final status.
        self._loop.create_task(self._handle_status_once_received(status_observer))

        cdef tuple outbound_ops
        cdef Operation initial_metadata_op = SendInitialMetadataOperation(
            outbound_initial_metadata,
            GRPC_INITIAL_METADATA_USED_MASK)
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

        # Sends out the request message.
        await execute_batch(self,
                            outbound_ops,
                            self._loop)

        # Receives initial metadata.
        initial_metadata_observer(
            await _receive_initial_metadata(self,
                                            self._loop),
        )

    async def stream_unary(self,
                           tuple outbound_initial_metadata,
                           object metadata_sent_observer,
                           object initial_metadata_observer,
                           object status_observer):
        """Actual implementation of the complete unary-stream call.
        
        Needs to pay extra attention to the raise mechanism. If we want to
        propagate the final status exception, then we have to raise it.
        Othersize, it would end normally and raise `StopAsyncIteration()`.
        """
        # Sends out initial_metadata ASAP.
        await _send_initial_metadata(self,
                                     outbound_initial_metadata,
                                     self._loop)
        # Notify upper level that sending messages are allowed now.
        metadata_sent_observer()

        # Receives initial metadata.
        initial_metadata_observer(
            await _receive_initial_metadata(self,
                                            self._loop),
        )

        cdef tuple inbound_ops
        cdef ReceiveMessageOperation receive_message_op = ReceiveMessageOperation(_EMPTY_FLAGS)
        cdef ReceiveStatusOnClientOperation receive_status_on_client_op = ReceiveStatusOnClientOperation(_EMPTY_FLAGS)
        inbound_ops = (receive_message_op, receive_status_on_client_op)

        # Executes all operations in one batch.
        await execute_batch(self,
                            inbound_ops,
                            self._loop)

        status = AioRpcStatus(
            receive_status_on_client_op.code(),
            receive_status_on_client_op.details(),
            receive_status_on_client_op.trailing_metadata(),
            receive_status_on_client_op.error_string(),
        )
        # Reports the final status of the RPC to Python layer. The observer
        # pattern is used here to unify unary and streaming code path.
        status_observer(status)

        if status.code() == StatusCode.ok:
            return receive_message_op.message()
        else:
            return None

    async def initiate_stream_stream(self,
                           tuple outbound_initial_metadata,
                           object metadata_sent_observer,
                           object initial_metadata_observer,
                           object status_observer):
        """Actual implementation of the complete stream-stream call.

        Needs to pay extra attention to the raise mechanism. If we want to
        propagate the final status exception, then we have to raise it.
        Othersize, it would end normally and raise `StopAsyncIteration()`.
        """
        # Peer may prematurely end this RPC at any point. We need a corutine
        # that watches if the server sends the final status.
        self._loop.create_task(self._handle_status_once_received(status_observer))

        # Sends out initial_metadata ASAP.
        await _send_initial_metadata(self,
                                     outbound_initial_metadata,
                                     self._loop)
        # Notify upper level that sending messages are allowed now.   
        metadata_sent_observer()

        # Receives initial metadata.
        initial_metadata_observer(
            await _receive_initial_metadata(self,
                                            self._loop),
        )
