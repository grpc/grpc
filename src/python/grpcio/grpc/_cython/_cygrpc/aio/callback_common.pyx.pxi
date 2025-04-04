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

    def __cinit__(self, object future, object loop, CallbackFailureHandler failure_handler):
        self.context.functor.functor_run = self.functor_run
        self.context.waiter = <cpython.PyObject*>future
        self.context.loop = <cpython.PyObject*>loop
        self.context.failure_handler = <cpython.PyObject*>failure_handler
        self.context.callback_wrapper = <cpython.PyObject*>self
        # NOTE(lidiz) Not using a list here, because this class is critical in
        # data path. We should make it as efficient as possible.
        self._reference_of_future = future
        self._reference_of_failure_handler = failure_handler
        # NOTE(lidiz) We need to ensure when Core invokes our callback, the
        # callback function itself is not deallocated. Otherwise, we will get
        # a segfault. We can view this as Core holding a ref.
        cpython.Py_INCREF(self)

    @staticmethod
    cdef void functor_run(
            grpc_completion_queue_functor* functor,
            int success) noexcept:
        cdef CallbackContext *context = <CallbackContext *>functor
        cdef object waiter = <object>context.waiter
        if not waiter.cancelled():
            if success == 0:
                (<CallbackFailureHandler>context.failure_handler).handle(waiter)
            else:
                waiter.set_result(None)
        cpython.Py_DECREF(<object>context.callback_wrapper)

    cdef grpc_completion_queue_functor *c_functor(self):
        return &self.context.functor


cdef CallbackFailureHandler CQ_SHUTDOWN_FAILURE_HANDLER = CallbackFailureHandler(
    'grpc_completion_queue_shutdown',
    'Unknown',
    InternalError)


class ExecuteBatchError(InternalError):
    """Raised when execute batch returns a failure from Core."""


async def execute_batch(GrpcCallWrapper grpc_call_wrapper,
                               tuple operations,
                               object loop):
    """The callback version of start batch operations."""
    cdef _BatchOperationTag batch_operation_tag = _BatchOperationTag(None, operations, None)
    batch_operation_tag.prepare()

    cdef object future = loop.create_future()
    cdef CallbackWrapper wrapper = CallbackWrapper(
        future,
        loop,
        CallbackFailureHandler('execute_batch', operations, ExecuteBatchError))
    cdef grpc_call_error error = grpc_call_start_batch(
        grpc_call_wrapper.call,
        batch_operation_tag.c_ops,
        batch_operation_tag.c_nops,
        wrapper.c_functor(), NULL)

    if error != GRPC_CALL_OK:
        grpc_call_error_string = grpc_call_error_to_string(error).decode()
        raise ExecuteBatchError("Failed grpc_call_start_batch: {} with grpc_call_error value: '{}'".format(error, grpc_call_error_string))

    await future

    cdef grpc_event c_event
    # Tag.event must be called, otherwise messages won't be parsed from C
    batch_operation_tag.event(c_event)


cdef prepend_send_initial_metadata_op(tuple ops, tuple metadata):
    # Eventually, this function should be the only function that produces
    # SendInitialMetadataOperation. So we have more control over the flag.
    return (SendInitialMetadataOperation(
        metadata,
        _EMPTY_FLAG
    ),) + ops


async def _receive_message(GrpcCallWrapper grpc_call_wrapper,
                           object loop):
    """Retrieves parsed messages from Core.

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
        _LOGGER.debug('Failed to receive any message from Core')
    # NOTE(lidiz) The returned message might be an empty bytes (aka. b'').
    # Please explicitly check if it is None or falsey string object!
    return receive_op.message()


async def _send_message(GrpcCallWrapper grpc_call_wrapper,
                        bytes message,
                        Operation send_initial_metadata_op,
                        int write_flag,
                        object loop):
    cdef SendMessageOperation op = SendMessageOperation(message, write_flag)
    cdef tuple ops = (op,)
    if send_initial_metadata_op is not None:
        ops = (send_initial_metadata_op,) + ops
    await execute_batch(grpc_call_wrapper, ops, loop)


async def _send_initial_metadata(GrpcCallWrapper grpc_call_wrapper,
                                 tuple metadata,
                                 int flags,
                                 object loop):
    cdef SendInitialMetadataOperation op = SendInitialMetadataOperation(
        metadata,
        flags)
    cdef tuple ops = (op,)
    await execute_batch(grpc_call_wrapper, ops, loop)


async def _receive_initial_metadata(GrpcCallWrapper grpc_call_wrapper,
                                    object loop):
    cdef ReceiveInitialMetadataOperation op = ReceiveInitialMetadataOperation(_EMPTY_FLAGS)
    cdef tuple ops = (op,)
    await execute_batch(grpc_call_wrapper, ops, loop)
    return op.initial_metadata()

async def _send_error_status_from_server(GrpcCallWrapper grpc_call_wrapper,
                                         grpc_status_code code,
                                         str details,
                                         tuple trailing_metadata,
                                         Operation send_initial_metadata_op,
                                         object loop):
    assert code != StatusCode.ok, 'Expecting non-ok status code.'
    cdef SendStatusFromServerOperation op = SendStatusFromServerOperation(
        trailing_metadata,
        code,
        details,
        _EMPTY_FLAGS,
    )
    cdef tuple ops = (op,)
    if send_initial_metadata_op is not None:
        ops = (send_initial_metadata_op,) + ops
    await execute_batch(grpc_call_wrapper, ops, loop)
