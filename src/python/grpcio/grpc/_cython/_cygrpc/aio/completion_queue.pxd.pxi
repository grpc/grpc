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
        // Discard the return value via a variable to satisfy
        // __attribute__((warn_unused_result)) under -Werror=unused-result.
        ssize_t rc = write(fd, "1", 1);
        (void)rc;
    #endif
    }
    """
    inline void _unified_socket_write_impl(int fd) nogil


cdef void _unified_socket_write(int fd) noexcept nogil

# One event loop's mailbox, filled by the poller thread without the GIL:
# the events owned by that loop plus write end of the loop's private wake-up
# socket. A plain C++ object so the nogil poller can use it through a raw ptr
cdef cppclass _LoopMailbox:
    mutex mtx
    cpp_event_queue events
    int write_fd


cdef class BaseCompletionQueue:
    cdef grpc_completion_queue *_cq

    cdef grpc_completion_queue* c_ptr(self)


cdef class _BoundEventLoop:
    cdef readonly object loop
    cdef object _read_socket    # socket.socket
    cdef object _write_socket   # socket.socket
    cdef _LoopMailbox _mailbox
    cdef object _loop_owning_thread_id
    cdef bint _has_reader

    cdef _drain_queue(self, bint complete_futures)
    cdef close_read_socket(self)
    cdef close_write_socket(self)


cdef class PollerCompletionQueue(BaseCompletionQueue):
    cdef atomic[bint] _shutdown
    cdef mutex _mailboxes_mutex
    cdef unordered_map[size_t, _LoopMailbox*] _mailboxes
    cdef object _poller_thread  # threading.Thread
    cdef dict _loops            # Mapping[asyncio.AbstractLoop, _BoundEventLoop]

    cdef int _poll(self) except -1 nogil
    cdef void _dispatch(self, grpc_event event) noexcept nogil
    cdef shutdown(self)
    cdef _unbind_loops(self)
