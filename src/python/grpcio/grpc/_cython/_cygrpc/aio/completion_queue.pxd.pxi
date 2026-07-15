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


ctypedef queue[grpc_event] cpp_event_queue


cdef extern from *:
    """
    #ifdef _WIN32
    #include <winsock2.h>
    #else
    #include <unistd.h>
    #endif

    static void _unified_socket_write_impl(int fd) {
    #ifdef _WIN32
        send((SOCKET)fd, "1", 1, 0);
    #else
        write(fd, "1", 1);
    #endif
    }
    """
    inline void _unified_socket_write_impl(int fd) nogil


cdef void _unified_socket_write(int fd) noexcept nogil


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

    cdef int _poll(self) except -1 nogil
    cdef shutdown(self)
