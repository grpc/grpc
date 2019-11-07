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
        if success == 0:
            (<CallbackFailureHandler>context.failure_handler).handle(
                <object>context.waiter)
        else:
            (<object>context.waiter).set_result(None)

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


async def callback_start_batch(GrpcCallWrapper grpc_call_wrapper,
                               tuple operations,
                               object loop):
    """The callback version of start batch operations."""
    cdef _BatchOperationTag batch_operation_tag = _BatchOperationTag(None, operations, None)
    batch_operation_tag.prepare()

    cdef object future = loop.create_future()
    cdef CallbackWrapper wrapper = CallbackWrapper(
        future,
        CallbackFailureHandler('callback_start_batch', operations, RuntimeError))
    # NOTE(lidiz) Without Py_INCREF, the wrapper object will be destructed
    # when calling "await". This is an over-optimization by Cython.
    cpython.Py_INCREF(wrapper)
    cdef grpc_call_error error = grpc_call_start_batch(
        grpc_call_wrapper.call,
        batch_operation_tag.c_ops,
        batch_operation_tag.c_nops,
        wrapper.c_functor(), NULL)

    if error != GRPC_CALL_OK:
        raise RuntimeError("Failed grpc_call_start_batch: {}".format(error))

    await future
    cpython.Py_DECREF(wrapper)
    cdef grpc_event c_event
    # Tag.event must be called, otherwise messages won't be parsed from C
    batch_operation_tag.event(c_event)
