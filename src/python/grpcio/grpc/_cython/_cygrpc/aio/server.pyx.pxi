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


import inspect
import traceback
import functools


cdef int _EMPTY_FLAG = 0
cdef str _RPC_FINISHED_DETAILS = 'RPC already finished.'
cdef str _SERVER_STOPPED_DETAILS = 'Server already stopped.'

cdef _augment_metadata(tuple metadata, object compression):
    if compression is None:
        return metadata
    else:
        return ((
            GRPC_COMPRESSION_REQUEST_ALGORITHM_MD_KEY,
            _COMPRESSION_METADATA_STRING_MAPPING[compression]
        ),) + metadata


cdef class _HandlerCallDetails:
    def __cinit__(self, str method, tuple invocation_metadata):
        self.method = method
        self.invocation_metadata = invocation_metadata


class _ServerStoppedError(BaseError):
    """Raised if the server is stopped."""


cdef class RPCState:

    def __cinit__(self, AioServer server):
        init_grpc_aio()
        self.call = NULL
        self.server = server
        grpc_metadata_array_init(&self.request_metadata)
        grpc_call_details_init(&self.details)
        self.client_closed = False
        self.abort_exception = None
        self.metadata_sent = False
        self.status_sent = False
        self.status_code = StatusCode.ok
        self.status_details = ''
        self.trailing_metadata = _IMMUTABLE_EMPTY_METADATA
        self.compression_algorithm = None
        self.disable_next_compression = False

    cdef bytes method(self):
        return _slice_bytes(self.details.method)

    cdef tuple invocation_metadata(self):
        return _metadata(&self.request_metadata)

    cdef void raise_for_termination(self) except *:
        """Raise exceptions if RPC is not running.

        Server method handlers may suppress the abort exception. We need to halt
        the RPC execution in that case. This function needs to be called after
        running application code.

        Also, the server may stop unexpected. We need to check before calling
        into Core functions, otherwise, segfault.
        """
        if self.abort_exception is not None:
            raise self.abort_exception
        if self.status_sent:
            raise UsageError(_RPC_FINISHED_DETAILS)
        if self.server._status == AIO_SERVER_STATUS_STOPPED:
            raise _ServerStoppedError(_SERVER_STOPPED_DETAILS)

    cdef int get_write_flag(self):
        if self.disable_next_compression:
            self.disable_next_compression = False
            return WriteFlag.no_compress
        else:
            return _EMPTY_FLAG

    cdef Operation create_send_initial_metadata_op_if_not_sent(self):
        cdef SendInitialMetadataOperation op
        if self.metadata_sent:
            return None
        else:
            op = SendInitialMetadataOperation(
                _augment_metadata(_IMMUTABLE_EMPTY_METADATA, self.compression_algorithm),
                _EMPTY_FLAG
            )
            return op

    def __dealloc__(self):
        """Cleans the Core objects."""
        grpc_call_details_destroy(&self.details)
        grpc_metadata_array_destroy(&self.request_metadata)
        if self.call:
            grpc_call_unref(self.call)
        shutdown_grpc_aio()


cdef class _ServicerContext:

    def __cinit__(self,
                  RPCState rpc_state,
                  object request_deserializer,
                  object response_serializer,
                  object loop):
        self._rpc_state = rpc_state
        self._request_deserializer = request_deserializer
        self._response_serializer = response_serializer
        self._loop = loop

    async def read(self):
        cdef bytes raw_message
        self._rpc_state.raise_for_termination()

        raw_message = await _receive_message(self._rpc_state, self._loop)
        self._rpc_state.raise_for_termination()

        if raw_message is None:
            return EOF
        else:
            return deserialize(self._request_deserializer,
                            raw_message)

    async def write(self, object message):
        self._rpc_state.raise_for_termination()

        await _send_message(self._rpc_state,
                            serialize(self._response_serializer, message),
                            self._rpc_state.create_send_initial_metadata_op_if_not_sent(),
                            self._rpc_state.get_write_flag(),
                            self._loop)
        self._rpc_state.metadata_sent = True

    async def send_initial_metadata(self, object metadata):
        self._rpc_state.raise_for_termination()

        if self._rpc_state.metadata_sent:
            raise UsageError('Send initial metadata failed: already sent')
        else:
            await _send_initial_metadata(
                self._rpc_state,
                _augment_metadata(tuple(metadata), self._rpc_state.compression_algorithm),
                _EMPTY_FLAG,
                self._loop
            )
            self._rpc_state.metadata_sent = True

    async def abort(self,
              object code,
              str details='',
              tuple trailing_metadata=_IMMUTABLE_EMPTY_METADATA):
        if self._rpc_state.abort_exception is not None:
            raise UsageError('Abort already called!')
        else:
            # Keeps track of the exception object. After abort happen, the RPC
            # should stop execution. However, if users decided to suppress it, it
            # could lead to undefined behavior.
            self._rpc_state.abort_exception = AbortError('Locally aborted.')

            if trailing_metadata == _IMMUTABLE_EMPTY_METADATA and self._rpc_state.trailing_metadata:
                trailing_metadata = self._rpc_state.trailing_metadata

            if details == '' and self._rpc_state.status_details:
                details = self._rpc_state.status_details

            actual_code = get_status_code(code)

            self._rpc_state.status_sent = True
            await _send_error_status_from_server(
                self._rpc_state,
                actual_code,
                details,
                trailing_metadata,
                self._rpc_state.create_send_initial_metadata_op_if_not_sent(),
                self._loop
            )

            raise self._rpc_state.abort_exception

    async def abort_with_status(self, object status):
        await self.abort(status.code, status.details, status.trailing_metadata)

    def set_trailing_metadata(self, object metadata):
        self._rpc_state.trailing_metadata = tuple(metadata)

    def trailing_metadata(self):
        return self._rpc_state.trailing_metadata

    def invocation_metadata(self):
        return self._rpc_state.invocation_metadata()

    def set_code(self, object code):
        self._rpc_state.status_code = get_status_code(code)

    def code(self):
        return self._rpc_state.status_code

    def set_details(self, str details):
        self._rpc_state.status_details = details

    def details(self):
        return self._rpc_state.status_details

    def set_compression(self, object compression):
        if self._rpc_state.metadata_sent:
            raise RuntimeError('Compression setting must be specified before sending initial metadata')
        else:
            self._rpc_state.compression_algorithm = compression

    def disable_next_message_compression(self):
        self._rpc_state.disable_next_compression = True

    def peer(self):
        cdef char *c_peer = NULL
        c_peer = grpc_call_get_peer(self._rpc_state.call)
        peer = (<bytes>c_peer).decode('utf8')
        gpr_free(c_peer)
        return peer

    def peer_identities(self):
        cdef Call query_call = Call()
        query_call.c_call = self._rpc_state.call
        identities = peer_identities(query_call)
        query_call.c_call = NULL
        return identities

    def peer_identity_key(self):
        cdef Call query_call = Call()
        query_call.c_call = self._rpc_state.call
        identity_key = peer_identity_key(query_call)
        query_call.c_call = NULL
        if identity_key:
            return identity_key.decode('utf8')
        else:
            return None

    def auth_context(self):
        cdef Call query_call = Call()
        query_call.c_call = self._rpc_state.call
        bytes_ctx = auth_context(query_call)
        query_call.c_call = NULL
        if bytes_ctx:
            ctx = {}
            for key in bytes_ctx:
                ctx[key.decode('utf8')] = bytes_ctx[key]
            return ctx
        else:
            return {}

    def time_remaining(self):
        if self._rpc_state.details.deadline.seconds == _GPR_INF_FUTURE.seconds:
            return None
        else:
            return max(_time_from_timespec(self._rpc_state.details.deadline) - time.time(), 0)


cdef class _SyncServicerContext:
    """Sync servicer context for sync handler compatibility."""

    def __cinit__(self,
                  _ServicerContext context):
        self._context = context
        self._callbacks = []
        self._loop = context._loop

    def abort(self,
              object code,
              str details='',
              tuple trailing_metadata=_IMMUTABLE_EMPTY_METADATA):
        future = asyncio.run_coroutine_threadsafe(
            self._context.abort(code, details, trailing_metadata),
            self._loop)
        # Abort should raise an AbortError
        future.exception()

    def send_initial_metadata(self, object metadata):
        future = asyncio.run_coroutine_threadsafe(
            self._context.send_initial_metadata(metadata),
            self._loop)
        future.result()

    def set_trailing_metadata(self, object metadata):
        self._context.set_trailing_metadata(metadata)

    def invocation_metadata(self):
        return self._context.invocation_metadata()

    def set_code(self, object code):
        self._context.set_code(code)

    def set_details(self, str details):
        self._context.set_details(details)

    def set_compression(self, object compression):
        self._context.set_compression(compression)

    def disable_next_message_compression(self):
        self._context.disable_next_message_compression()

    def add_callback(self, object callback):
        self._callbacks.append(callback)

    def peer(self):
        return self._context.peer()

    def peer_identities(self):
        return self._context.peer_identities()

    def peer_identity_key(self):
        return self._context.peer_identity_key()

    def auth_context(self):
        return self._context.auth_context()

    def time_remaining(self):
        return self._context.time_remaining()


async def _run_interceptor(object interceptors, object query_handler,
                           object handler_call_details):
    interceptor = next(interceptors, None)
    if interceptor:
        continuation = functools.partial(_run_interceptor, interceptors,
                                         query_handler)
        return await interceptor.intercept_service(continuation, handler_call_details)
    else:
        return query_handler(handler_call_details)


def _is_async_handler(object handler):
    """Inspect if a method handler is async or sync."""
    return inspect.isawaitable(handler) or inspect.iscoroutinefunction(handler) or inspect.isasyncgenfunction(handler)


async def _find_method_handler(str method, tuple metadata, list generic_handlers,
                          tuple interceptors):
    def query_handlers(handler_call_details):
        for generic_handler in generic_handlers:
            method_handler = generic_handler.service(handler_call_details)
            if method_handler is not None:
                return method_handler
        return None

    cdef _HandlerCallDetails handler_call_details = _HandlerCallDetails(method,
                                                                        metadata)
    # interceptor
    if interceptors:
        return await _run_interceptor(iter(interceptors), query_handlers,
                                      handler_call_details)
    else:
        return query_handlers(handler_call_details)


async def _finish_handler_with_unary_response(RPCState rpc_state,
                                              object unary_handler,
                                              object request,
                                              _ServicerContext servicer_context,
                                              object response_serializer,
                                              object loop):
    """Finishes server method handler with a single response.

    This function executes the application handler, and handles response
    sending, as well as errors. It is shared between unary-unary and
    stream-unary handlers.
    """
    # Executes application logic
    cdef object response_message
    cdef _SyncServicerContext sync_servicer_context

    if _is_async_handler(unary_handler):
        # Run async method handlers in this coroutine
        response_message = await unary_handler(
            request,
            servicer_context,
        )
    else:
        # Run sync method handlers in the thread pool
        sync_servicer_context = _SyncServicerContext(servicer_context)
        response_message = await loop.run_in_executor(
            rpc_state.server.thread_pool(),
            unary_handler,
            request,
            sync_servicer_context,
        )
        # Support sync-stack callback
        for callback in sync_servicer_context._callbacks:
            callback()

    # Raises exception if aborted
    rpc_state.raise_for_termination()

    # Serializes the response message
    cdef bytes response_raw
    if rpc_state.status_code == StatusCode.ok:
        response_raw = serialize(
            response_serializer,
            response_message,
        )
    else:
        # Discards the response message if the status code is non-OK.
        response_raw = b''

    # Assembles the batch operations
    cdef tuple finish_ops
    finish_ops = (
        SendMessageOperation(response_raw, rpc_state.get_write_flag()),
        SendStatusFromServerOperation(
            rpc_state.trailing_metadata,
            rpc_state.status_code,
            rpc_state.status_details,
            _EMPTY_FLAGS,
        ),
    )
    if not rpc_state.metadata_sent:
        finish_ops = prepend_send_initial_metadata_op(
            finish_ops,
            None)
    rpc_state.metadata_sent = True
    rpc_state.status_sent = True
    await execute_batch(rpc_state, finish_ops, loop)


async def _finish_handler_with_stream_responses(RPCState rpc_state,
                                                object stream_handler,
                                                object request,
                                                _ServicerContext servicer_context,
                                                object loop):
    """Finishes server method handler with multiple responses.

    This function executes the application handler, and handles response
    sending, as well as errors. It is shared between unary-stream and
    stream-stream handlers.
    """
    cdef object async_response_generator
    cdef object response_message

    if inspect.iscoroutinefunction(stream_handler):
        # Case 1: Coroutine async handler - using reader-writer API
        # The handler uses reader / writer API, returns None.
        await stream_handler(
            request,
            servicer_context,
        )
    else:
        if inspect.isasyncgenfunction(stream_handler):
            # Case 2: Async handler - async generator
            # The handler uses async generator API
            async_response_generator = stream_handler(
                request,
                servicer_context,
            )
        else:
            # Case 3: Sync handler - normal generator
            # NOTE(lidiz) Streaming handler in sync stack is either a generator
            # function or a function returns a generator.
            sync_servicer_context = _SyncServicerContext(servicer_context)
            gen = stream_handler(request, sync_servicer_context)
            async_response_generator = generator_to_async_generator(gen,
                                                                    loop,
                                                                    rpc_state.server.thread_pool())

        # Consumes messages from the generator
        async for response_message in async_response_generator:
            # Raises exception if aborted
            rpc_state.raise_for_termination()

            await servicer_context.write(response_message)

    # Raises exception if aborted
    rpc_state.raise_for_termination()

    # Sends the final status of this RPC
    cdef SendStatusFromServerOperation op = SendStatusFromServerOperation(
        rpc_state.trailing_metadata,
        rpc_state.status_code,
        rpc_state.status_details,
        _EMPTY_FLAGS,
    )

    cdef tuple finish_ops = (op,)
    if not rpc_state.metadata_sent:
        finish_ops = prepend_send_initial_metadata_op(
            finish_ops,
            None
        )
    rpc_state.metadata_sent = True
    rpc_state.status_sent = True
    await execute_batch(rpc_state, finish_ops, loop)


async def _handle_unary_unary_rpc(object method_handler,
                                  RPCState rpc_state,
                                  object loop):
    # Receives request message
    cdef bytes request_raw = await _receive_message(rpc_state, loop)
    if request_raw is None:
        # The RPC was cancelled immediately after start on client side.
        return

    # Deserializes the request message
    cdef object request_message = deserialize(
        method_handler.request_deserializer,
        request_raw,
    )

    # Creates a dedecated ServicerContext
    cdef _ServicerContext servicer_context = _ServicerContext(
        rpc_state,
        None,
        None,
        loop,
    )

    # Finishes the application handler
    await _finish_handler_with_unary_response(
        rpc_state,
        method_handler.unary_unary,
        request_message,
        servicer_context,
        method_handler.response_serializer,
        loop
    )


async def _handle_unary_stream_rpc(object method_handler,
                                   RPCState rpc_state,
                                   object loop):
    # Receives request message
    cdef bytes request_raw = await _receive_message(rpc_state, loop)
    if request_raw is None:
        return

    # Deserializes the request message
    cdef object request_message = deserialize(
        method_handler.request_deserializer,
        request_raw,
    )

    # Creates a dedecated ServicerContext
    cdef _ServicerContext servicer_context = _ServicerContext(
        rpc_state,
        method_handler.request_deserializer,
        method_handler.response_serializer,
        loop,
    )

    # Finishes the application handler
    await _finish_handler_with_stream_responses(
        rpc_state,
        method_handler.unary_stream,
        request_message,
        servicer_context,
        loop,
    )


cdef class _MessageReceiver:
    """Bridge between the async generator API and the reader-writer API."""

    def __cinit__(self, _ServicerContext servicer_context):
        self._servicer_context = servicer_context
        self._agen = None

    async def _async_message_receiver(self):
        """An async generator that receives messages."""
        cdef object message
        while True:
            message = await self._servicer_context.read()
            if message is not EOF:
                yield message
            else:
                break

    def __aiter__(self):
        # Prevents never awaited warning if application never used the async generator
        if self._agen is None:
            self._agen = self._async_message_receiver()
        return self._agen

    async def __anext__(self):
        return await self.__aiter__().__anext__()


async def _handle_stream_unary_rpc(object method_handler,
                                   RPCState rpc_state,
                                   object loop):
    # Creates a dedecated ServicerContext
    cdef _ServicerContext servicer_context = _ServicerContext(
        rpc_state,
        method_handler.request_deserializer,
        None,
        loop,
    )

    # Prepares the request generator
    cdef object request_iterator
    if _is_async_handler(method_handler.stream_unary):
        request_iterator = _MessageReceiver(servicer_context)
    else:
        request_iterator = async_generator_to_generator(
            _MessageReceiver(servicer_context),
            loop
        )

    # Finishes the application handler
    await _finish_handler_with_unary_response(
        rpc_state,
        method_handler.stream_unary,
        request_iterator,
        servicer_context,
        method_handler.response_serializer,
        loop
    )


async def _handle_stream_stream_rpc(object method_handler,
                                    RPCState rpc_state,
                                    object loop):
    # Creates a dedecated ServicerContext
    cdef _ServicerContext servicer_context = _ServicerContext(
        rpc_state,
        method_handler.request_deserializer,
        method_handler.response_serializer,
        loop,
    )

    # Prepares the request generator
    cdef object request_iterator
    if _is_async_handler(method_handler.stream_stream):
        request_iterator = _MessageReceiver(servicer_context)
    else:
        request_iterator = async_generator_to_generator(
            _MessageReceiver(servicer_context),
            loop
        )

    # Finishes the application handler
    await _finish_handler_with_stream_responses(
        rpc_state,
        method_handler.stream_stream,
        request_iterator,
        servicer_context,
        loop,
    )


async def _handle_exceptions(RPCState rpc_state, object rpc_coro, object loop):
    try:
        try:
            await rpc_coro
        except AbortError as e:
            # Caught AbortError check if it is the same one
            assert rpc_state.abort_exception is e, 'Abort error has been replaced!'
            return
        else:
            # Check if the abort exception got suppressed
            if rpc_state.abort_exception is not None:
                _LOGGER.error(
                    'Abort error unexpectedly suppressed: %s',
                    traceback.format_exception(rpc_state.abort_exception)
                )
    except (KeyboardInterrupt, SystemExit):
        raise
    except asyncio.CancelledError:
        _LOGGER.debug('RPC cancelled for servicer method [%s]', _decode(rpc_state.method()))
    except _ServerStoppedError:
        _LOGGER.warning('Aborting method [%s] due to server stop.', _decode(rpc_state.method()))
    except ExecuteBatchError:
        # If client closed (aka. cancelled), ignore the failed batch operations.
        if rpc_state.client_closed:
            return
        else:
            raise
    except Exception as e:
        _LOGGER.exception('Unexpected [%s] raised by servicer method [%s]' % (
            type(e).__name__,
            _decode(rpc_state.method()),
        ))
        if not rpc_state.status_sent and rpc_state.server._status != AIO_SERVER_STATUS_STOPPED:
            # Allows users to raise other types of exception with specified status code
            if rpc_state.status_code == StatusCode.ok:
                status_code = StatusCode.unknown
            else:
                status_code = rpc_state.status_code

            await _send_error_status_from_server(
                rpc_state,
                status_code,
                'Unexpected %s: %s' % (type(e), e),
                rpc_state.trailing_metadata,
                rpc_state.create_send_initial_metadata_op_if_not_sent(),
                loop
            )


async def _handle_cancellation_from_core(object rpc_task,
                                         RPCState rpc_state,
                                         object loop):
    cdef ReceiveCloseOnServerOperation op = ReceiveCloseOnServerOperation(_EMPTY_FLAG)
    cdef tuple ops = (op,)

    # Awaits cancellation from peer.
    await execute_batch(rpc_state, ops, loop)
    rpc_state.client_closed = True
    # If 1) received cancel signal; 2) the Task is not finished; 3) the server
    # wasn't replying final status. For condition 3, it might cause inaccurate
    # log that an RPC is both aborted and cancelled.
    if op.cancelled() and not rpc_task.done() and not rpc_state.status_sent:
        # Injects `CancelledError` to halt the RPC coroutine
        rpc_task.cancel()


async def _schedule_rpc_coro(object rpc_coro,
                             RPCState rpc_state,
                             object loop):
    # Schedules the RPC coroutine.
    cdef object rpc_task = loop.create_task(_handle_exceptions(
        rpc_state,
        rpc_coro,
        loop,
    ))
    await _handle_cancellation_from_core(rpc_task, rpc_state, loop)


async def _handle_rpc(list generic_handlers, tuple interceptors,
                      RPCState rpc_state, object loop):
    cdef object method_handler
    # Finds the method handler (application logic)
    method_handler = await _find_method_handler(
        rpc_state.method().decode(),
        rpc_state.invocation_metadata(),
        generic_handlers,
        interceptors,
    )
    if method_handler is None:
        rpc_state.status_sent = True
        await _send_error_status_from_server(
            rpc_state,
            StatusCode.unimplemented,
            'Method not found!',
            _IMMUTABLE_EMPTY_METADATA,
            rpc_state.create_send_initial_metadata_op_if_not_sent(),
            loop
        )
        return

    # Handles unary-unary case
    if not method_handler.request_streaming and not method_handler.response_streaming:
        await _handle_unary_unary_rpc(method_handler,
                                      rpc_state,
                                      loop)
        return

    # Handles unary-stream case
    if not method_handler.request_streaming and method_handler.response_streaming:
        await _handle_unary_stream_rpc(method_handler,
                                       rpc_state,
                                       loop)
        return

    # Handles stream-unary case
    if method_handler.request_streaming and not method_handler.response_streaming:
        await _handle_stream_unary_rpc(method_handler,
                                       rpc_state,
                                       loop)
        return

    # Handles stream-stream case
    if method_handler.request_streaming and method_handler.response_streaming:
        await _handle_stream_stream_rpc(method_handler,
                                        rpc_state,
                                        loop)
        return


class _RequestCallError(Exception): pass

cdef CallbackFailureHandler REQUEST_CALL_FAILURE_HANDLER = CallbackFailureHandler(
    'grpc_server_request_call', None, _RequestCallError)


cdef CallbackFailureHandler SERVER_SHUTDOWN_FAILURE_HANDLER = CallbackFailureHandler(
    'grpc_server_shutdown_and_notify',
    None,
    InternalError)


cdef class _ConcurrentRpcLimiter:

    def __cinit__(self, int maximum_concurrent_rpcs, object loop):
        if maximum_concurrent_rpcs <= 0:
            raise ValueError("maximum_concurrent_rpcs should be a postive integer")
        self._maximum_concurrent_rpcs = maximum_concurrent_rpcs
        self._active_rpcs = 0
        self._active_rpcs_condition = asyncio.Condition()
        self._loop = loop

    async def check_before_request_call(self):
        await self._active_rpcs_condition.acquire()
        try:
            predicate = lambda: self._active_rpcs < self._maximum_concurrent_rpcs
            await self._active_rpcs_condition.wait_for(predicate)
            self._active_rpcs += 1
        finally:
            self._active_rpcs_condition.release()

    async def _decrease_active_rpcs_count_with_lock(self):
        await self._active_rpcs_condition.acquire()
        try:
            self._active_rpcs -= 1
            self._active_rpcs_condition.notify()
        finally:
            self._active_rpcs_condition.release()

    def _decrease_active_rpcs_count(self, unused_future):
        self._loop.create_task(self._decrease_active_rpcs_count_with_lock())

    def decrease_once_finished(self, object rpc_task):
        rpc_task.add_done_callback(self._decrease_active_rpcs_count)


cdef class AioServer:

    def __init__(self, loop, thread_pool, generic_handlers, interceptors,
                 options, maximum_concurrent_rpcs):
        init_grpc_aio()
        # NOTE(lidiz) Core objects won't be deallocated automatically.
        # If AioServer.shutdown is not called, those objects will leak.
        # TODO(rbellevi): Support xDS in aio server.
        self._server = Server(options, False)
        grpc_server_register_completion_queue(
            self._server.c_server,
            global_completion_queue(),
            NULL
        )

        self._loop = loop
        self._status = AIO_SERVER_STATUS_READY
        self._generic_handlers = []
        self.add_generic_rpc_handlers(generic_handlers)
        self._serving_task = None

        self._shutdown_lock = asyncio.Lock(loop=self._loop)
        self._shutdown_completed = self._loop.create_future()
        self._shutdown_callback_wrapper = CallbackWrapper(
            self._shutdown_completed,
            self._loop,
            SERVER_SHUTDOWN_FAILURE_HANDLER)
        self._crash_exception = None

        if interceptors:
            self._interceptors = interceptors
        else:
            self._interceptors = ()

        self._thread_pool = thread_pool
        if maximum_concurrent_rpcs is not None:
            self._limiter = _ConcurrentRpcLimiter(maximum_concurrent_rpcs,
                                                  loop)

    def add_generic_rpc_handlers(self, object generic_rpc_handlers):
        self._generic_handlers.extend(generic_rpc_handlers)

    def add_insecure_port(self, address):
        return self._server.add_http2_port(address)

    def add_secure_port(self, address, server_credentials):
        return self._server.add_http2_port(address,
                                           server_credentials._credentials)

    async def _request_call(self):
        cdef grpc_call_error error
        cdef RPCState rpc_state = RPCState(self)
        cdef object future = self._loop.create_future()
        cdef CallbackWrapper wrapper = CallbackWrapper(
            future,
            self._loop,
            REQUEST_CALL_FAILURE_HANDLER)
        error = grpc_server_request_call(
            self._server.c_server, &rpc_state.call, &rpc_state.details,
            &rpc_state.request_metadata,
            global_completion_queue(), global_completion_queue(),
            wrapper.c_functor()
        )
        if error != GRPC_CALL_OK:
            raise InternalError("Error in grpc_server_request_call: %s" % error)

        await future
        return rpc_state

    async def _server_main_loop(self,
                                object server_started):
        self._server.start(backup_queue=False)
        cdef RPCState rpc_state
        server_started.set_result(True)

        while True:
            # When shutdown begins, no more new connections.
            if self._status != AIO_SERVER_STATUS_RUNNING:
                break

            if self._limiter is not None:
                await self._limiter.check_before_request_call()

            # Accepts new request from Core
            rpc_state = await self._request_call()

            # Creates the dedicated RPC coroutine. If we schedule it right now,
            # there is no guarantee if the cancellation listening coroutine is
            # ready or not. So, we should control the ordering by scheduling
            # the coroutine onto event loop inside of the cancellation
            # coroutine.
            rpc_coro = _handle_rpc(self._generic_handlers,
                                   self._interceptors,
                                   rpc_state,
                                   self._loop)

            # Fires off a task that listens on the cancellation from client.
            rpc_task = self._loop.create_task(
                _schedule_rpc_coro(
                    rpc_coro,
                    rpc_state,
                    self._loop
                )
            )

            if self._limiter is not None:
                self._limiter.decrease_once_finished(rpc_task)

    def _serving_task_crash_handler(self, object task):
        """Shutdown the server immediately if unexpectedly exited."""
        if task.cancelled():
            return
        if task.exception() is None:
            return
        if self._status != AIO_SERVER_STATUS_STOPPING:
            self._crash_exception = task.exception()
            _LOGGER.exception(self._crash_exception)
            self._loop.create_task(self.shutdown(None))

    async def start(self):
        if self._status == AIO_SERVER_STATUS_RUNNING:
            return
        elif self._status != AIO_SERVER_STATUS_READY:
            raise UsageError('Server not in ready state')

        self._status = AIO_SERVER_STATUS_RUNNING
        cdef object server_started = self._loop.create_future()
        self._serving_task = self._loop.create_task(self._server_main_loop(server_started))
        self._serving_task.add_done_callback(self._serving_task_crash_handler)
        # Needs to explicitly wait for the server to start up.
        # Otherwise, the actual start time of the server is un-controllable.
        await server_started

    async def _start_shutting_down(self):
        """Prepares the server to shutting down.

        This coroutine function is NOT coroutine-safe.
        """
        # The shutdown callback won't be called until there is no live RPC.
        grpc_server_shutdown_and_notify(
            self._server.c_server,
            global_completion_queue(),
            self._shutdown_callback_wrapper.c_functor())

        # Ensures the serving task (coroutine) exits.
        try:
            await self._serving_task
        except _RequestCallError:
            pass

    async def shutdown(self, grace):
        """Gracefully shutdown the Core server.

        Application should only call shutdown once.

        Args:
          grace: An optional float indicating the length of grace period in
            seconds.
        """
        if self._status == AIO_SERVER_STATUS_READY or self._status == AIO_SERVER_STATUS_STOPPED:
            return

        async with self._shutdown_lock:
            if self._status == AIO_SERVER_STATUS_RUNNING:
                self._server.is_shutting_down = True
                self._status = AIO_SERVER_STATUS_STOPPING
                await self._start_shutting_down()

        if grace is None:
            # Directly cancels all calls
            grpc_server_cancel_all_calls(self._server.c_server)
            await self._shutdown_completed
        else:
            try:
                await asyncio.wait_for(
                    asyncio.shield(
                        self._shutdown_completed,
                        loop=self._loop
                    ),
                    grace,
                    loop=self._loop,
                )
            except asyncio.TimeoutError:
                # Cancels all ongoing calls by the end of grace period.
                grpc_server_cancel_all_calls(self._server.c_server)
                await self._shutdown_completed

        async with self._shutdown_lock:
            if self._status == AIO_SERVER_STATUS_STOPPING:
                grpc_server_destroy(self._server.c_server)
                self._server.c_server = NULL
                self._server.is_shutdown = True
                self._status = AIO_SERVER_STATUS_STOPPED

    async def wait_for_termination(self, object timeout):
        if timeout is None:
            await self._shutdown_completed
        else:
            try:
                await asyncio.wait_for(
                    asyncio.shield(
                        self._shutdown_completed,
                        loop=self._loop,
                    ),
                    timeout,
                    loop=self._loop,
                )
            except asyncio.TimeoutError:
                if self._crash_exception is not None:
                    raise self._crash_exception
                return False
        if self._crash_exception is not None:
            raise self._crash_exception
        return True

    def __dealloc__(self):
        """Deallocation of Core objects are ensured by Python layer."""
        # TODO(lidiz) if users create server, and then dealloc it immediately.
        # There is a potential memory leak of created Core server.
        if self._status != AIO_SERVER_STATUS_STOPPED:
            _LOGGER.debug(
                '__dealloc__ called on running server %s with status %d',
                self,
                self._status
            )
        shutdown_grpc_aio()

    cdef thread_pool(self):
        """Access the thread pool instance."""
        return self._thread_pool

    def is_running(self):
        return self._status == AIO_SERVER_STATUS_RUNNING
