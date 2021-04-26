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
        self._timer_future = None
        self._active = False
        self._loop = get_working_loop()
        cpython.Py_INCREF(self)

    @staticmethod
    cdef _AsyncioTimer create(grpc_custom_timer * grpc_timer, float timeout):
        timer = _AsyncioTimer()
        timer._grpc_timer = grpc_timer
        timer._timer_future = timer._loop.call_later(timeout, timer.on_time_up)
        timer._active = True
        return timer

    def on_time_up(self):
        self._active = False
        grpc_custom_timer_callback(self._grpc_timer, <grpc_error_handle>0)
        cpython.Py_DECREF(self)

    def __repr__(self):
        class_name = self.__class__.__name__ 
        id_ = id(self)
        return f"<{class_name} {id_} deadline={self._deadline} active={self._active}>"

    cdef stop(self):
        if not self._active:
            return

        self._timer_future.cancel()
        self._active = False
        cpython.Py_DECREF(self)
