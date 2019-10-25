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
_EMPTY_METADATA = None
_OP_ARRAY_LENGTH = 6


cdef class _AioCall:

    def __cinit__(self, AioChannel channel):
        self._channel = channel
        self._functor.functor_run = _AioCall.functor_run

        self._cq = grpc_completion_queue_create_for_callback(
            <grpc_experimental_completion_queue_functor *> &self._functor,
            NULL
        )

        self._watcher_call.functor.functor_run = _AioCall.watcher_call_functor_run
        self._watcher_call.waiter = <cpython.PyObject *> self
        self._waiter_call = None

    def __dealloc__(self):
        grpc_completion_queue_shutdown(self._cq)
        grpc_completion_queue_destroy(self._cq)

    def __repr__(self):
        class_name = self.__class__.__name__
        id_ = id(self)
        return f"<{class_name} {id_}>"

    @staticmethod
    cdef void functor_run(grpc_experimental_completion_queue_functor* functor, int succeed):
        pass

    @staticmethod
    cdef void watcher_call_functor_run(grpc_experimental_completion_queue_functor* functor, int succeed):
        call = <_AioCall>(<CallbackContext *>functor).waiter

        assert call._waiter_call

        if succeed == 0:
            call._waiter_call.set_exception(Exception("Some error occurred"))
        else:
            call._waiter_call.set_result(None)

    async def unary_unary(self, method, request, timeout):
        cdef grpc_call * call
        cdef grpc_slice method_slice
        cdef grpc_op * ops

        cdef Operation initial_metadata_operation
        cdef Operation send_message_operation
        cdef Operation send_close_from_client_operation
        cdef Operation receive_initial_metadata_operation
        cdef Operation receive_message_operation
        cdef Operation receive_status_on_client_operation

        cdef grpc_call_error call_status
        cdef gpr_timespec deadline = _timespec_from_time(timeout)

        method_slice = grpc_slice_from_copied_buffer(
            <const char *> method,
            <size_t> len(method)
        )

        call = grpc_channel_create_call(
            self._channel.channel,
            NULL,
            0,
            self._cq,
            method_slice,
            NULL,
            deadline,
            NULL
        )

        grpc_slice_unref(method_slice)

        ops = <grpc_op *>gpr_malloc(sizeof(grpc_op) * _OP_ARRAY_LENGTH)

        initial_metadata_operation = SendInitialMetadataOperation(_EMPTY_METADATA, GRPC_INITIAL_METADATA_USED_MASK)
        initial_metadata_operation.c()
        ops[0] = <grpc_op> initial_metadata_operation.c_op

        send_message_operation = SendMessageOperation(request, _EMPTY_FLAGS)
        send_message_operation.c()
        ops[1] = <grpc_op> send_message_operation.c_op

        send_close_from_client_operation = SendCloseFromClientOperation(_EMPTY_FLAGS)
        send_close_from_client_operation.c()
        ops[2] = <grpc_op> send_close_from_client_operation.c_op

        receive_initial_metadata_operation = ReceiveInitialMetadataOperation(_EMPTY_FLAGS)
        receive_initial_metadata_operation.c()
        ops[3] = <grpc_op> receive_initial_metadata_operation.c_op

        receive_message_operation = ReceiveMessageOperation(_EMPTY_FLAGS)
        receive_message_operation.c()
        ops[4] = <grpc_op> receive_message_operation.c_op

        receive_status_on_client_operation = ReceiveStatusOnClientOperation(_EMPTY_FLAGS)
        receive_status_on_client_operation.c()
        ops[5] = <grpc_op> receive_status_on_client_operation.c_op

        self._waiter_call = asyncio.get_event_loop().create_future()

        call_status = grpc_call_start_batch(
            call,
            ops,
            _OP_ARRAY_LENGTH,
            &self._watcher_call.functor,
            NULL
        )

        try:
            if call_status != GRPC_CALL_OK:
                self._waiter_call = None
                raise Exception("Error with grpc_call_start_batch {}".format(call_status))

            await self._waiter_call

        finally:
            initial_metadata_operation.un_c()
            send_message_operation.un_c()
            send_close_from_client_operation.un_c()
            receive_initial_metadata_operation.un_c()
            receive_message_operation.un_c()
            receive_status_on_client_operation.un_c()

            grpc_call_unref(call)
            gpr_free(ops)

        if receive_status_on_client_operation.code() == StatusCode.ok:
            return receive_message_operation.message()

        raise grpc.experimental.aio.AioRpcError(
            receive_initial_metadata_operation.initial_metadata(),
            receive_status_on_client_operation.code(),
            receive_status_on_client_operation.details(),
            receive_status_on_client_operation.trailing_metadata(),
        )
