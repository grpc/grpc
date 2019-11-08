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


cdef class _AioCall:

    def __cinit__(self, AioChannel channel):
        self._channel = channel
        self._references = []
        self._grpc_call_wrapper = GrpcCallWrapper()

    def __repr__(self):
        class_name = self.__class__.__name__
        id_ = id(self)
        return f"<{class_name} {id_}>"

    cdef grpc_call* _create_grpc_call(self,
                                      object timeout,
                                      bytes method) except *:
        """Creates the corresponding Core object for this RPC.

        For unary calls, the grpc_call lives shortly and can be destroied after
        invoke start_batch. However, if either side is streaming, the grpc_call
        life span will be longer than one function. So, it would better save it
        as an instance variable than a stack variable, which reflects its
        nature in Core.
        """
        cdef grpc_slice method_slice
        cdef gpr_timespec deadline = _timespec_from_time(timeout)

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
            deadline,
            NULL
        )
        grpc_slice_unref(method_slice)

    cdef void _destroy_grpc_call(self):
        """Destroys the corresponding Core object for this RPC."""
        grpc_call_unref(self._grpc_call_wrapper.call)

    async def unary_unary(self, bytes method, bytes request, object timeout, AioCancelStatus cancel_status):
        cdef object loop = asyncio.get_event_loop()

        cdef tuple operations
        cdef Operation initial_metadata_operation
        cdef Operation send_message_operation
        cdef Operation send_close_from_client_operation
        cdef Operation receive_initial_metadata_operation
        cdef Operation receive_message_operation
        cdef Operation receive_status_on_client_operation

        cdef char *c_details = NULL

        initial_metadata_operation = SendInitialMetadataOperation(_EMPTY_METADATA, GRPC_INITIAL_METADATA_USED_MASK)
        initial_metadata_operation.c()

        send_message_operation = SendMessageOperation(request, _EMPTY_FLAGS)
        send_message_operation.c()

        send_close_from_client_operation = SendCloseFromClientOperation(_EMPTY_FLAGS)
        send_close_from_client_operation.c()

        receive_initial_metadata_operation = ReceiveInitialMetadataOperation(_EMPTY_FLAGS)
        receive_initial_metadata_operation.c()

        receive_message_operation = ReceiveMessageOperation(_EMPTY_FLAGS)
        receive_message_operation.c()

        receive_status_on_client_operation = ReceiveStatusOnClientOperation(_EMPTY_FLAGS)
        receive_status_on_client_operation.c()

        operations = (
            initial_metadata_operation,
            send_message_operation,
            send_close_from_client_operation,
            receive_initial_metadata_operation,
            receive_message_operation,
            receive_status_on_client_operation,
        )

        try:
            self._create_grpc_call(
                timeout,
                method,
            )

            try:
                await callback_start_batch(
                    self._grpc_call_wrapper,
                    operations,
                    loop
                )
            except asyncio.CancelledError:
                if cancel_status:
                    details = str_to_bytes(cancel_status.details())
                    self._references.append(details)
                    c_details = <char *>details
                    call_status = grpc_call_cancel_with_status(
                        self._grpc_call_wrapper.call,
                        cancel_status.code(),
                        c_details,
                        NULL,
                    )
                else:
                    call_status = grpc_call_cancel(
                        self._grpc_call_wrapper.call, NULL)
                if call_status != GRPC_CALL_OK:
                    raise Exception("RPC call couldn't be cancelled. Error {}".format(call_status))
                raise
        finally:
            self._destroy_grpc_call()

        if receive_status_on_client_operation.code() == StatusCode.ok:
            return receive_message_operation.message()

        raise AioRpcError(
            receive_initial_metadata_operation.initial_metadata(),
            receive_status_on_client_operation.code(),
            receive_status_on_client_operation.details(),
            receive_status_on_client_operation.trailing_metadata(),
        )
