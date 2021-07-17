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

# NOTE(lidiz) Unfortunately, we can't use "cimport" here because Cython
# links it with exception handling. It introduces new dependencies.
cdef extern from "<queue>" namespace "std" nogil:
    cdef cppclass queue[T]:
        queue()
        bint empty()
        T& front()
        void pop()
        void push(T&)
        size_t size()


cdef extern from "<mutex>" namespace "std" nogil:
    cdef cppclass mutex:
        mutex()
        void lock()
        void unlock()


ctypedef queue[grpc_event] cpp_event_queue


IF UNAME_SYSNAME == "Windows":
    cdef extern from "winsock2.h" nogil:
        ctypedef uint32_t WIN_SOCKET "SOCKET"
        WIN_SOCKET win_socket "socket" (int af, int type, int protocol)
        int win_socket_send "send" (WIN_SOCKET s, const char *buf, int len, int flags)


cdef void _unified_socket_write(int fd) nogil


cdef class BaseCompletionQueue:
    cdef grpc_completion_queue *_cq

    cdef grpc_completion_queue* c_ptr(self)


cdef class _BoundEventLoop:
    cdef readonly object loop
    cdef readonly object read_socket  # socket.socket
    cdef bint _has_reader


cdef class PollerCompletionQueue(BaseCompletionQueue):
    cdef bint _shutdown
    cdef cpp_event_queue _queue
    cdef mutex _queue_mutex
    cdef object _poller_thread  # threading.Thread
    cdef int _write_fd
    cdef object _read_socket    # socket.socket
    cdef object _write_socket   # socket.socket
    cdef dict _loops            # Mapping[asyncio.AbstractLoop, _BoundEventLoop]

    cdef void _poll(self) nogil
    cdef shutdown(self)


cdef class CallbackCompletionQueue(BaseCompletionQueue):
    cdef object _shutdown_completed  # asyncio.Future
    cdef CallbackWrapper _wrapper
    cdef object _loop
