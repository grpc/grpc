# Copyright 2020 gRPC authors.
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

cdef class _IOLoop:
    cdef:
        cdef object _asyncio_loop
        cdef object _thread
        cdef object _io_ev
        cdef object _loop_started_ev

    cpdef void io_mark(self)
    cdef void io_wait(self, size_t timeout_ms)
    cdef object asyncio_loop(self)
