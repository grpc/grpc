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

cdef class BaseCompletionQueue:
    cdef grpc_completion_queue *_cq

    cdef grpc_completion_queue* c_ptr(self)

cdef class PollerCompletionQueue(BaseCompletionQueue):
    cdef bint _shutdown
    cdef object _poller_thread
    cdef object _loop

    cdef void _poll(self) except *
    cdef void shutdown(self) nogil


cdef class CallbackCompletionQueue(BaseCompletionQueue):
    cdef object _shutdown_completed  # asyncio.Future
    cdef CallbackWrapper _wrapper
    cdef object _loop
