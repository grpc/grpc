# Copyright 2019 gRPC authors.
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


cdef class _AsyncioTimer:
    def __cinit__(self):
        self._grpc_timer = NULL
        self._timer_handler = None
        self._active = 0
        self._loop = None

    @staticmethod
    cdef _AsyncioTimer create(grpc_custom_timer * grpc_timer, deadline):
        timer = _AsyncioTimer()
        timer._grpc_timer = grpc_timer
        timer._deadline = deadline
        timer._active = 1
        timer._loop = _current_io_loop().asyncio_loop()

        def callback():
            if timer._active == 1:
                timer._timer_handler = timer._loop.call_later(deadline, timer._on_deadline)

        timer._loop.call_soon_threadsafe(callback)
        return timer

    def _on_deadline(self):
        cdef grpc_error* error
        cdef grpc_custom_timer* timer = <grpc_custom_timer*> self._grpc_timer

        if self._active == 0:
            return

        self._active = 0
        
        error = grpc_error_none()
        with nogil:
            grpc_custom_timer_callback(timer, error)

        _current_io_loop().io_mark()

    def __repr__(self):
        class_name = self.__class__.__name__ 
        id_ = id(self)
        return f"<{class_name} {id_} deadline={self._deadline} active={self._active}>"

    cdef stop(self):
        if self._active == 0:
            return

        if self._timer_handler:
            self._loop.call_soon_threadsafe(self._timer_handler.cancel)

        self._active = 0
