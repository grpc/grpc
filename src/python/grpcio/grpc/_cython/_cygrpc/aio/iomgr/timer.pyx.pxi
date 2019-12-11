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

    @staticmethod
    cdef _AsyncioTimer create(grpc_custom_timer * grpc_timer, deadline):
        timer = _AsyncioTimer()
        timer._grpc_timer = grpc_timer
        timer._deadline = deadline
        timer._timer_handler = asyncio.get_event_loop().call_later(deadline, timer._on_deadline)
        timer._active = 1
        return timer

    def _on_deadline(self):
        self._active = 0
        grpc_custom_timer_callback(self._grpc_timer, <grpc_error*>0)

    def __repr__(self):
        class_name = self.__class__.__name__ 
        id_ = id(self)
        return f"<{class_name} {id_} deadline={self._deadline} active={self._active}>"

    cdef stop(self):
        if self._active == 0:
            return

        self._timer_handler.cancel()
        self._active = 0
