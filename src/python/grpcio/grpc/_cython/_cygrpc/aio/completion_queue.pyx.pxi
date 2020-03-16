# Copyright 2020 The gRPC Authors
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

from libc.stdio cimport printf

cdef gpr_timespec _GPR_INF_FUTURE = gpr_inf_future(GPR_CLOCK_REALTIME)


def _handle_callback_wrapper(CallbackWrapper callback_wrapper, int success):
    CallbackWrapper.functor_run(callback_wrapper.c_functor(), success)


cdef class BaseCompletionQueue:

    cdef grpc_completion_queue* c_ptr(self):
        return self._cq


cdef class PollerCompletionQueue(BaseCompletionQueue):

    def __cinit__(self):
        self._cq = grpc_completion_queue_create_for_next(NULL)
        self._shutdown = False
        self._poller_thread = threading.Thread(target=self._poll_wrapper, daemon=True)
        self._poller_thread.start()

    cdef void _poll(self) except *:
        cdef grpc_event event
        cdef CallbackContext *context

        while not self._shutdown:
            with nogil:
                event = grpc_completion_queue_next(self._cq,
                                                   _GPR_INF_FUTURE,
                                                   NULL)

            if event.type == GRPC_QUEUE_TIMEOUT:
                raise AssertionError("Core should not return GRPC_QUEUE_TIMEOUT!")
            elif event.type == GRPC_QUEUE_SHUTDOWN:
                self._shutdown = True
            else:
                context = <CallbackContext *>event.tag
                loop = <object>context.loop
                loop.call_soon_threadsafe(
                    _handle_callback_wrapper,
                    <CallbackWrapper>context.callback_wrapper,
                    event.success)

    def _poll_wrapper(self):
        self._poll()

    cdef void shutdown(self) nogil:
        # TODO(https://github.com/grpc/grpc/issues/22365) perform graceful shutdown
        grpc_completion_queue_shutdown(self._cq)
        grpc_completion_queue_destroy(self._cq)


cdef class CallbackCompletionQueue(BaseCompletionQueue):

    def __cinit__(self):
        self._loop = asyncio.get_event_loop()
        self._shutdown_completed = self._loop.create_future()
        self._wrapper = CallbackWrapper(
            self._shutdown_completed,
            self._loop,
            CQ_SHUTDOWN_FAILURE_HANDLER)
        self._cq = grpc_completion_queue_create_for_callback(
            self._wrapper.c_functor(),
            NULL
        )

    async def shutdown(self):
        grpc_completion_queue_shutdown(self._cq)
        await self._shutdown_completed
        grpc_completion_queue_destroy(self._cq)
