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

cdef gpr_timespec _GPR_INF_FUTURE = gpr_inf_future(GPR_CLOCK_REALTIME)


def _handle_callback_wrapper(CallbackWrapper callback_wrapper, int success):
    CallbackWrapper.functor_run(callback_wrapper.c_functor(), success)


cdef class BackgroundCompletionQueue:

    def __cinit__(self):
        self._cq = grpc_completion_queue_create_for_next(NULL)
        self._shutdown = False
        self._shutdown_completed = asyncio.get_event_loop().create_future()
        self._poller = None
        self._poller_running = asyncio.get_event_loop().create_future()
        self._poller = threading.Thread(target=self._polling_wrapper)
        self._poller.daemon = True
        self._poller.start()

    # async def _start_poller(self):
    #     if self._poller:
    #         raise UsageError('Poller can only be started once.')

    #     self._poller = threading.Thread(target=self._polling_wrapper)
    #     self._poller.daemon = True
    #     self._poller.start()
    #     await self._poller_running

    cdef _polling(self):
        cdef grpc_event event
        cdef CallbackContext *context
        cdef object waiter
        grpc_call_soon_threadsafe(self._poller_running.set_result, None)

        while not self._shutdown:
            _LOGGER.debug('BackgroundCompletionQueue polling')
            with nogil:
                event = grpc_completion_queue_next(self._cq,
                                                _GPR_INF_FUTURE,
                                                NULL)
            _LOGGER.debug('BackgroundCompletionQueue polling 1')

            if event.type == GRPC_QUEUE_TIMEOUT:
                _LOGGER.debug('BackgroundCompletionQueue timeout???')
                raise NotImplementedError()
            elif event.type == GRPC_QUEUE_SHUTDOWN:
                _LOGGER.debug('BackgroundCompletionQueue shutdown!')
                self._shutdown = True
                grpc_call_soon_threadsafe(self._shutdown_completed.set_result, None)
            else:
                _LOGGER.debug('BackgroundCompletionQueue event! %d', event.success)
                context = <CallbackContext *>event.tag
                grpc_call_soon_threadsafe(
                    _handle_callback_wrapper,
                    <CallbackWrapper>context.callback_wrapper,
                    event.success)
            _LOGGER.debug('BackgroundCompletionQueue polling 2')

    def _polling_wrapper(self):
        self._polling()

    async def shutdown(self):
        grpc_completion_queue_shutdown(self._cq)
        await self._shutdown_completed
        grpc_completion_queue_destroy(self._cq)

    cdef grpc_completion_queue* c_ptr(self):
        return self._cq
