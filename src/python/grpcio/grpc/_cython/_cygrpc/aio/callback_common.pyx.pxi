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


cdef class CallbackFailureHandler:
    
    def __cinit__(self,
                  str core_function_name,
                  object error_details,
                  object exception_type):
        """Handles failure by raising exception."""
        self._core_function_name = core_function_name
        self._error_details = error_details
        self._exception_type = exception_type

    cdef handle(self, object future):
        future.set_exception(self._exception_type(
            'Failed "%s": %s' % (self._core_function_name, self._error_details)
        ))


cdef class CallbackWrapper:

    def __cinit__(self, object future, CallbackFailureHandler failure_handler):
        self.context.functor.functor_run = self.functor_run
        self.context.waiter = <cpython.PyObject*>future
        self.context.failure_handler = <cpython.PyObject*>failure_handler
        # NOTE(lidiz) Not using a list here, because this class is critical in
        # data path. We should make it as efficient as possible.
        self._reference_of_future = future
        self._reference_of_failure_handler = failure_handler

    @staticmethod
    cdef void functor_run(
            grpc_experimental_completion_queue_functor* functor,
            int success):
        cdef CallbackContext *context = <CallbackContext *>functor
        cdef object waiter = <object>context.waiter
        if waiter.cancelled():
            return
        if success == 0:
            (<CallbackFailureHandler>context.failure_handler).handle(waiter)
        else:
            waiter.set_result(None)

    cdef grpc_experimental_completion_queue_functor *c_functor(self):
        return &self.context.functor


cdef CallbackFailureHandler CQ_SHUTDOWN_FAILURE_HANDLER = CallbackFailureHandler(
    'grpc_completion_queue_shutdown',
    'Unknown',
    RuntimeError)


cdef class CallbackCompletionQueue:

    def __cinit__(self):
        self._shutdown_completed = asyncio.get_event_loop().create_future()
        self._wrapper = CallbackWrapper(
            self._shutdown_completed,
            CQ_SHUTDOWN_FAILURE_HANDLER)
        self._cq = grpc_completion_queue_create_for_callback(
            self._wrapper.c_functor(),
            NULL
        )

    cdef grpc_completion_queue* c_ptr(self):
        return self._cq
    
    async def shutdown(self):
        grpc_completion_queue_shutdown(self._cq)
        await self._shutdown_completed
        grpc_completion_queue_destroy(self._cq)


class ExecuteBatchError(Exception): pass


async def execute_batch(GrpcCallWrapper grpc_call_wrapper,
                               tuple operations,
                               object loop):
    """The callback version of start batch operations."""
    cdef _BatchOperationTag batch_operation_tag = _BatchOperationTag(None, operations, None)
    batch_operation_tag.prepare()

    cdef object future = loop.create_future()
    cdef CallbackWrapper wrapper = CallbackWrapper(
        future,
        CallbackFailureHandler('execute_batch', operations, ExecuteBatchError))
    # NOTE(lidiz) Without Py_INCREF, the wrapper object will be destructed
    # when calling "await". This is an over-optimization by Cython.
    cpython.Py_INCREF(wrapper)
    cdef grpc_call_error error = grpc_call_start_batch(
        grpc_call_wrapper.call,
        batch_operation_tag.c_ops,
        batch_operation_tag.c_nops,
        wrapper.c_functor(), NULL)

    if error != GRPC_CALL_OK:
        raise ExecuteBatchError("Failed grpc_call_start_batch: {}".format(error))

    await future
    cpython.Py_DECREF(wrapper)
    cdef grpc_event c_event
    # Tag.event must be called, otherwise messages won't be parsed from C
    batch_operation_tag.event(c_event)


async def _receive_message(GrpcCallWrapper grpc_call_wrapper,
                           object loop):
    """Retrives parsed messages from Core.

    The messages maybe already in Core's buffer, so there isn't a 1-to-1
    mapping between this and the underlying "socket.read()". Also, eventually,
    this function will end with an EOF, which reads empty message.
    """
    cdef ReceiveMessageOperation receive_op = ReceiveMessageOperation(_EMPTY_FLAG)
    cdef tuple ops = (receive_op,)
    try:
        await execute_batch(grpc_call_wrapper, ops, loop)
    except ExecuteBatchError as e:
        # NOTE(lidiz) The receive message operation has two ways to indicate
        # finish state : 1) returns empty message due to EOF; 2) fails inside
        # the callback (e.g. cancelled).
        #
        # Since they all indicates finish, they are better be merged.
        _LOGGER.debug(e)
    return receive_op.message()


async def _send_message(GrpcCallWrapper grpc_call_wrapper,
                        bytes message,
                        bint metadata_sent,
                        object loop):
    cdef SendMessageOperation op = SendMessageOperation(message, _EMPTY_FLAG)
    cdef tuple ops
    if metadata_sent:
        ops = (op,)
    else:
        ops = (
            # Initial metadata must be sent before first outbound message.
            SendInitialMetadataOperation(None, _EMPTY_FLAG),
            op,
        )
    await execute_batch(grpc_call_wrapper, ops, loop)


async def _send_initial_metadata(GrpcCallWrapper grpc_call_wrapper,
                                 tuple metadata,
                                 object loop):
    cdef SendInitialMetadataOperation op = SendInitialMetadataOperation(
        metadata,
        _EMPTY_FLAG)
    cdef tuple ops = (op,)
    await execute_batch(grpc_call_wrapper, ops, loop)


async def _receive_initial_metadata(GrpcCallWrapper grpc_call_wrapper,
                                    object loop):
    cdef ReceiveInitialMetadataOperation op = ReceiveInitialMetadataOperation(_EMPTY_FLAGS)
    cdef tuple ops = (op,)
    await execute_batch(grpc_call_wrapper, ops, loop)
    return op.initial_metadata()
