# Copyright 2019 The gRPC Authors
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

_LOGGER = logging.getLogger(__name__)


cdef class _HandlerCallDetails:
    def __cinit__(self, str method, tuple invocation_metadata):
        self.method = method
        self.invocation_metadata = invocation_metadata


class _ServicerContextPlaceHolder(object): pass


cdef class CallbackFailureHandler:
    cdef str _c_core_api
    cdef object _error_details
    cdef object _exception_type
    cdef object _callback  # Callable[[Future], None]

    def __cinit__(self,
                  str c_core_api="",
                  object error_details="UNKNOWN",
                  object exception_type=RuntimeError,
                  object callback=None):
        """Handles failure by raising exception or execute a callbcak.
        
        The callback accepts a future, returns nothing. The callback is
        expected to finish the future either "set_result" or "set_exception".
        """
        if callback is None:
            self._c_core_api = c_core_api
            self._error_details = error_details
            self._exception_type = exception_type    
            self._callback = self._raise_exception
        else:
            self._callback = callback

    def _raise_exception(self, object future):
        future.set_exception(self._exception_type(
            'Failed "%s": %s' % (self._c_core_api, self._error_details)
        ))

    cdef handle(self, object future):
        self._callback(future)


# TODO(https://github.com/grpc/grpc/issues/20669)
# Apply this to the client-side
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
        if succeed == 0:
            (<CallbackFailureHandler>context.failure_handler).handle(
                <object>context.waiter)
        else:
            (<object>context.waiter).set_result(None)

    cdef grpc_experimental_completion_queue_functor *c_functor(self):
        return &self.context.functor


cdef class RPCState:

    def __cinit__(self):
        grpc_metadata_array_init(&self.request_metadata)
        grpc_call_details_init(&self.details)

    cdef bytes method(self):
      return _slice_bytes(self.details.method)

    def __dealloc__(self):
        """Cleans the Core objects."""
        grpc_call_details_destroy(&self.details)
        grpc_metadata_array_destroy(&self.request_metadata)
        if self.call:
            grpc_call_unref(self.call)


cdef _find_method_handler(str method, list generic_handlers):
    # TODO(lidiz) connects Metadata to call details
    cdef _HandlerCallDetails handler_call_details = _HandlerCallDetails(
        method,
        tuple()
    )

    for generic_handler in generic_handlers:
        method_handler = generic_handler.service(handler_call_details)
        if method_handler is not None:
            return method_handler
    return None


async def callback_start_batch(RPCState rpc_state,
                               tuple operations,
                               object loop):
    """The callback version of start batch operations."""
    cdef _BatchOperationTag batch_operation_tag = _BatchOperationTag(None, operations, None)
    batch_operation_tag.prepare()

    cdef object future = loop.create_future()
    cdef CallbackWrapper wrapper = CallbackWrapper(
        future,
        CallbackFailureHandler('callback_start_batch', operations))
    # NOTE(lidiz) Without Py_INCREF, the wrapper object will be destructed
    # when calling "await". This is an over-optimization by Cython.
    cpython.Py_INCREF(wrapper)
    cdef grpc_call_error error = grpc_call_start_batch(
        rpc_state.call,
        batch_operation_tag.c_ops,
        batch_operation_tag.c_nops,
        wrapper.c_functor(), NULL)

    if error != GRPC_CALL_OK:
        raise RuntimeError("Error with callback_start_batch {}".format(error))

    await future
    cpython.Py_DECREF(wrapper)
    cdef grpc_event c_event
    # Tag.event must be called, otherwise messages won't be parsed from C
    batch_operation_tag.event(c_event)


async def _handle_unary_unary_rpc(object method_handler,
                                  RPCState rpc_state,
                                  object loop):
    # Receives request message
    cdef tuple receive_ops = (
        ReceiveMessageOperation(_EMPTY_FLAGS),
    )
    await callback_start_batch(rpc_state, receive_ops, loop)

    # Deserializes the request message
    cdef bytes request_raw = receive_ops[0].message()
    cdef object request_message
    if method_handler.request_deserializer:
        request_message = method_handler.request_deserializer(request_raw)
    else:
        request_message = request_raw

    # Executes application logic
    cdef object response_message = await method_handler.unary_unary(request_message, _ServicerContextPlaceHolder())

    # Serializes the response message
    cdef bytes response_raw
    if method_handler.response_serializer:
        response_raw = method_handler.response_serializer(response_message)
    else:
        response_raw = response_message

    # Sends response message
    cdef tuple send_ops = (
        SendStatusFromServerOperation(
        tuple(), StatusCode.ok, b'', _EMPTY_FLAGS),
        SendInitialMetadataOperation(tuple(), _EMPTY_FLAGS),
        SendMessageOperation(response_raw, _EMPTY_FLAGS),
    )
    await callback_start_batch(rpc_state, send_ops, loop)


async def _handle_rpc(list generic_handlers, RPCState rpc_state, object loop):
    # Finds the method handler (application logic)
    cdef object method_handler = _find_method_handler(
        rpc_state.method().decode(),
        generic_handlers
    )
    if method_handler is None:
        # TODO(lidiz) return unimplemented error to client side
        raise NotImplementedError()
    # TODO(lidiz) extend to all 4 types of RPC
    if method_handler.request_streaming or method_handler.response_streaming:
        raise NotImplementedError()
    else:
        await _handle_unary_unary_rpc(
            method_handler,
            rpc_state,
            loop
        )


class _RequestCallError(Exception): pass

cdef CallbackFailureHandler REQUEST_CALL_FAILURE_HANDLER = CallbackFailureHandler(
    'grpc_server_request_call', 'server shutdown', _RequestCallError)


async def _server_call_request_call(Server server,
                                    _CallbackCompletionQueue cq,
                                    object loop):
    cdef grpc_call_error error
    cdef RPCState rpc_state = RPCState()
    cdef object future = loop.create_future()
    cdef CallbackWrapper wrapper = CallbackWrapper(
        future,
        REQUEST_CALL_FAILURE_HANDLER)
    # NOTE(lidiz) Without Py_INCREF, the wrapper object will be destructed
    # when calling "await". This is an over-optimization by Cython.
    cpython.Py_INCREF(wrapper)
    error = grpc_server_request_call(
        server.c_server, &rpc_state.call, &rpc_state.details,
        &rpc_state.request_metadata,
        cq.c_ptr(), cq.c_ptr(),
        wrapper.c_functor()
    )
    if error != GRPC_CALL_OK:
        raise RuntimeError("Error in _server_call_request_call: %s" % error)

    await future
    cpython.Py_DECREF(wrapper)
    return rpc_state


cdef CallbackFailureHandler CQ_SHUTDOWN_FAILURE_HANDLER = CallbackFailureHandler('grpc_completion_queue_shutdown')


cdef class _CallbackCompletionQueue:

    def __cinit__(self, object loop):
        self._loop = loop
        self._shutdown_completed = loop.create_future()
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


cdef CallbackFailureHandler SERVER_SHUTDOWN_FAILURE_HANDLER = CallbackFailureHandler('grpc_server_shutdown_and_notify')


cdef class AioServer:

    def __init__(self, loop, thread_pool, generic_handlers, interceptors,
                 options, maximum_concurrent_rpcs, compression):
        # C-Core objects won't be deallocated automatically.
        self._server = Server(options)
        self._cq = _CallbackCompletionQueue(loop)
        grpc_server_register_completion_queue(
            self._server.c_server,
            self._cq.c_ptr(),
            NULL
        )

        self._loop = loop
        self._status = AIO_SERVER_STATUS_READY
        self._generic_handlers = []
        self.add_generic_rpc_handlers(generic_handlers)
        self._serving_task = None

        if interceptors:
            raise NotImplementedError()
        if maximum_concurrent_rpcs:
            raise NotImplementedError()
        if compression:
            raise NotImplementedError()
        if thread_pool:
            raise NotImplementedError()

    def add_generic_rpc_handlers(self, generic_rpc_handlers):
        for h in generic_rpc_handlers:
            self._generic_handlers.append(h)

    def add_insecure_port(self, address):
        return self._server.add_http2_port(address)

    def add_secure_port(self, address, server_credentials):
        return self._server.add_http2_port(address,
                                          server_credentials._credentials)

    async def _server_main_loop(self,
                                object server_started):
        self._server.start(backup_queue=False)
        server_started.set_result(True)
        cdef RPCState rpc_state

        while True:
            # When shutdown process starts, no more new connections.
            if self._status != AIO_SERVER_STATUS_RUNNING:
                break

            rpc_state = await _server_call_request_call(
                self._server,
                self._cq,
                self._loop)

            self._loop.create_task(_handle_rpc(
                self._generic_handlers,
                rpc_state,
                self._loop))

    async def start(self):
        if self._status == AIO_SERVER_STATUS_RUNNING:
            return
        elif self._status != AIO_SERVER_STATUS_READY:
            raise RuntimeError('Server not in ready state')

        self._status = AIO_SERVER_STATUS_RUNNING
        cdef object server_started = self._loop.create_future()
        self._serving_task = self._loop.create_task(self._server_main_loop(server_started))
        # Needs to explicitly wait for the server to start up.
        # Otherwise, the actual start time of the server is un-controllable.
        await server_started

    async def shutdown(self, grace):
        """Gracefully shutdown the C-Core server.

        Application should only call shutdown once.

        Args:
          grace: An optional float indicates the length of grace period in
            seconds.
        """
        if self._status != AIO_SERVER_STATUS_RUNNING:
            # The server either is shutting down, or not started.
            return
        cdef object shutdown_completed = self._loop.create_future()
        cdef CallbackWrapper wrapper = CallbackWrapper(
            shutdown_completed,
            SERVER_SHUTDOWN_FAILURE_HANDLER)
        # NOTE(lidiz) Without Py_INCREF, the wrapper object will be destructed
        # when calling "await". This is an over-optimization by Cython.
        cpython.Py_INCREF(wrapper)

        # Starts the shutdown process.
        # The shutdown callback won't be called unless there is no live RPC.
        grpc_server_shutdown_and_notify(
            self._server.c_server,
            self._cq._cq,
            wrapper.c_functor())
        self._server.is_shutting_down = True
        self._status = AIO_SERVER_STATUS_STOPPING

        # Ensures the serving task (coroutine) exits.
        try:
            await self._serving_task
        except _RequestCallError:
            pass

        if grace is None:
            # Directly cancels all calls
            grpc_server_cancel_all_calls(self._server.c_server)
            await shutdown_completed
        else:
            try:
                await asyncio.wait_for(shutdown_completed, grace)
            except asyncio.TimeoutError:
                # Cancels all ongoing calls by the end of grace period.
                grpc_server_cancel_all_calls(self._server.c_server)
                await shutdown_completed

        # Keeps wrapper object alive until now.
        cpython.Py_DECREF(wrapper)
        grpc_server_destroy(self._server.c_server)
        self._server.c_server = NULL
        self._server.is_shutdown = True
        self._status = AIO_SERVER_STATUS_STOPPED

        # Shuts down the completion queue
        await self._cq.shutdown()

    def __dealloc__(self):
        if self._status != AIO_SERVER_STATUS_STOPPED:
            _LOGGER.error('Server is not stopped while deallocation: %d', self._status)
