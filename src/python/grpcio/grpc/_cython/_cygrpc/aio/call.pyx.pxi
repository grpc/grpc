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


cdef class _AioCall:

    def __cinit__(self,
                  AioChannel channel,
                  object deadline,
                  bytes method):
        self._channel = channel
        self._references = []
        self._grpc_call_wrapper = GrpcCallWrapper()
        self._loop = asyncio.get_event_loop()
        self._create_grpc_call(deadline, method)

        self._status_received = asyncio.Event(loop=self._loop)

    def __dealloc__(self):
        self._destroy_grpc_call()

    def __repr__(self):
        class_name = self.__class__.__name__
        id_ = id(self)
        return f"<{class_name} {id_}>"

    cdef grpc_call* _create_grpc_call(self,
                                      object deadline,
                                      bytes method) except *:
        """Creates the corresponding Core object for this RPC.

        For unary calls, the grpc_call lives shortly and can be destroied after
        invoke start_batch. However, if either side is streaming, the grpc_call
        life span will be longer than one function. So, it would better save it
        as an instance variable than a stack variable, which reflects its
        nature in Core.
        """
        cdef grpc_slice method_slice
        cdef gpr_timespec c_deadline = _timespec_from_time(deadline)

        method_slice = grpc_slice_from_copied_buffer(
            <const char *> method,
            <size_t> len(method)
        )
        self._grpc_call_wrapper.call = grpc_channel_create_call(
            self._channel.channel,
            NULL,
            _EMPTY_MASK,
            self._channel.cq.c_ptr(),
            method_slice,
            NULL,
            c_deadline,
            NULL
        )
        grpc_slice_unref(method_slice)

    cdef void _destroy_grpc_call(self):
        """Destroys the corresponding Core object for this RPC."""
        grpc_call_unref(self._grpc_call_wrapper.call)

    cdef AioRpcStatus _cancel_and_create_status(self, object cancellation_future):
        """Cancels the RPC in Core, and return the final RPC status."""
        cdef AioRpcStatus status
        cdef object details
        cdef char *c_details
        cdef grpc_call_error error
        # Try to fetch application layer cancellation details in the future.
        # * If cancellation details present, cancel with status;
        # * If details not present, cancel with unknown reason.
        if cancellation_future.done():
            status = cancellation_future.result()
            details = str_to_bytes(status.details())
            self._references.append(details)
            c_details = <char *>details
            # By implementation, grpc_call_cancel_with_status always return OK
            error = grpc_call_cancel_with_status(
                self._grpc_call_wrapper.call,
                status.c_code(),
                c_details,
                NULL,
            )
            assert error == GRPC_CALL_OK
            return status
        else:
            # By implementation, grpc_call_cancel always return OK
            error = grpc_call_cancel(self._grpc_call_wrapper.call, NULL)
            assert error == GRPC_CALL_OK
            status = AioRpcStatus(
                StatusCode.cancelled,
                _UNKNOWN_CANCELLATION_DETAILS,
                None,
                None,
            )
            cancellation_future.set_result(status)
            return status

    async def unary_unary(self,
                          bytes request,
                          object cancellation_future,
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
            _EMPTY_METADATA,
            GRPC_INITIAL_METADATA_USED_MASK)
        cdef SendMessageOperation send_message_op = SendMessageOperation(request, _EMPTY_FLAGS)
        cdef SendCloseFromClientOperation send_close_op = SendCloseFromClientOperation(_EMPTY_FLAGS)
        cdef ReceiveInitialMetadataOperation receive_initial_metadata_op = ReceiveInitialMetadataOperation(_EMPTY_FLAGS)
        cdef ReceiveMessageOperation receive_message_op = ReceiveMessageOperation(_EMPTY_FLAGS)
        cdef ReceiveStatusOnClientOperation receive_status_on_client_op = ReceiveStatusOnClientOperation(_EMPTY_FLAGS)

        ops = (initial_metadata_op, send_message_op, send_close_op,
               receive_initial_metadata_op, receive_message_op,
               receive_status_on_client_op)

        try:
            await execute_batch(self._grpc_call_wrapper,
                                        ops,
                                        self._loop)
        except asyncio.CancelledError:
            status = self._cancel_and_create_status(cancellation_future)
            initial_metadata_observer(None)
            status_observer(status)
            raise
        else:
            initial_metadata_observer(
                receive_initial_metadata_op.initial_metadata()
            )

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
        await execute_batch(self._grpc_call_wrapper, ops, self._loop)
        cdef AioRpcStatus status = AioRpcStatus(
            op.code(),
            op.details(),
            op.trailing_metadata(),
            op.error_string(),
        )
        status_observer(status)
        self._status_received.set()

    def _handle_cancellation_from_application(self,
                                              object cancellation_future,
                                              object status_observer):
        def _cancellation_action(finished_future):
            if not self._status_received.set():
                status = self._cancel_and_create_status(finished_future)
                status_observer(status)
                self._status_received.set()

        cancellation_future.add_done_callback(_cancellation_action)

    async def _message_async_generator(self):
        cdef bytes received_message

        # Infinitely receiving messages, until:
        # * EOF, no more messages to read;
        # * The client application cancells;
        # * The server sends final status.
        while True:
            if self._status_received.is_set():
                return

            received_message = await _receive_message(
                self._grpc_call_wrapper,
                self._loop
            )
            if received_message is None:
                # The read operation failed, Core should explain why it fails
                await self._status_received.wait()
                return
            else:
                yield received_message

    async def unary_stream(self,
                           bytes request,
                           object cancellation_future,
                           object initial_metadata_observer,
                           object status_observer):
        """Actual implementation of the complete unary-stream call.
        
        Needs to pay extra attention to the raise mechanism. If we want to
        propagate the final status exception, then we have to raise it.
        Othersize, it would end normally and raise `StopAsyncIteration()`.
        """
        cdef tuple outbound_ops
        cdef Operation initial_metadata_op = SendInitialMetadataOperation(
            _EMPTY_METADATA,
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

        # Actually sends out the request message.
        await execute_batch(self._grpc_call_wrapper,
                                   outbound_ops,
                                   self._loop)

        # Peer may prematurely end this RPC at any point. We need a mechanism
        # that handles both the normal case and the error case.
        self._loop.create_task(self._handle_status_once_received(status_observer))
        self._handle_cancellation_from_application(cancellation_future,
                                                    status_observer)

        # Receives initial metadata.
        initial_metadata_observer(
            await _receive_initial_metadata(self._grpc_call_wrapper,
                                            self._loop),
        )

        return self._message_async_generator()
