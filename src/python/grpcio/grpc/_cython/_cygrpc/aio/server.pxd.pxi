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

cdef class _HandlerCallDetails:
    cdef readonly str method
    cdef readonly tuple invocation_metadata


cdef class RPCState:
    cdef grpc_call* call,
    cdef grpc_call_details details
    cdef grpc_metadata_array request_metadata

    cdef bytes method(self)


cdef class CallbackWrapper:
    cdef CallbackContext context
    cdef object _reference_of_future
    cdef object _reference_of_failure_handler

    @staticmethod
    cdef void functor_run(
            grpc_experimental_completion_queue_functor* functor,
            int succeed)

    cdef grpc_experimental_completion_queue_functor *c_functor(self)


cdef enum AioServerStatus:
    AIO_SERVER_STATUS_UNKNOWN
    AIO_SERVER_STATUS_READY
    AIO_SERVER_STATUS_RUNNING
    AIO_SERVER_STATUS_STOPPED
    AIO_SERVER_STATUS_STOPPING


cdef class _CallbackCompletionQueue:
    cdef grpc_completion_queue *_cq
    cdef grpc_completion_queue* c_ptr(self)
    cdef object _shutdown_completed  # asyncio.Future
    cdef CallbackWrapper _wrapper
    cdef object _loop  # asyncio.EventLoop


cdef class AioServer:
    cdef Server _server
    cdef _CallbackCompletionQueue _cq
    cdef list _generic_handlers
    cdef AioServerStatus _status
    cdef object _loop  # asyncio.EventLoop
    cdef object _serving_task  # asyncio.Task
    cdef object _shutdown_lock  # asyncio.Lock
    cdef object _shutdown_completed  # asyncio.Future
    cdef CallbackWrapper _shutdown_callback_wrapper
    cdef object _crash_exception  # Exception
