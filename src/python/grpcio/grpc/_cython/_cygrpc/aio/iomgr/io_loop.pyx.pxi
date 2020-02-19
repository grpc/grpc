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
import sys
import threading

_RESOLUTION_MS = 0.001
cdef _IOLoop _io_loop = None

cdef class _IOLoop:

    def __init__(self):
        global _io_loop

        # Only one instantiation is exepected        
        assert _io_loop is None

        self._asyncio_loop = None
        self._io_ev = threading.Event()
        self._loop_started_cv = threading.Condition()
        self._loop_started = False

        self._thread = threading.Thread(target=self._run_forever, daemon=True)
        self._thread.start()

        # Some attributes are initialitzated when
        # the thread is really started, we wait till
        # the whole Asyncio loop is initialized and
        # ready to be used.
        with self._loop_started_cv:
            if not self._loop_started:
                self._loop_started_cv.wait()

        _io_loop = self


    def _loop_started_cb(self):
        with self._loop_started_cv:
            self._loop_started = True
            self._loop_started_cv.notify_all()

    def _run_forever(self):
        self._asyncio_loop = asyncio.new_event_loop()
        asyncio.set_event_loop(self._asyncio_loop)
        self._asyncio_loop.call_soon(self._loop_started_cb)
        try:
            self._asyncio_loop.run_forever()
        except Exception as exp:
            print("An error ocurred with the IO loop {}".format(exp))
            # Without the IO loop running the program would become
            # unresponsive, proactively we close the process.
            sys.exit(1)

    cdef inline void io_mark(self):
        # Wake up all threads that were waiting
        # for an IO event.
        self._io_ev.set()

        # Clear the status, further threads will get
        # block.
        self._io_ev.clear()

    cdef void io_wait(self, size_t timeout_ms):
        if threading.get_ident() == self._thread.ident:
            # Reentrance is not allowed, otherwise we will be
            # blocking the Asyncio loop.
            return

        if timeout_ms > 0:
            self._io_ev.wait(timeout_ms * _RESOLUTION_MS)

    cdef object asyncio_loop(self):
        return self._asyncio_loop


cdef _IOLoop _current_io_loop():
    global _io_loop
    return _io_loop

cdef inline void _fast_io_mark():
    global _io_loop
    _io_loop.io_mark()
