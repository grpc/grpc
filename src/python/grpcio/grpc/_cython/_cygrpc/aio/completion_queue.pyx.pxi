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

import socket

cdef gpr_timespec _GPR_INF_FUTURE = gpr_inf_future(GPR_CLOCK_REALTIME)
cdef float _POLL_AWAKE_INTERVAL_S = 0.2
cdef int _WAKEUP_READ_SIZE = 4096

# This bool indicates if the event loop impl can monitor a given fd, or has
# loop.add_reader method.
cdef atomic[bint] _has_fd_monitoring
_has_fd_monitoring.store(True)

cdef void _unified_socket_write(int fd) noexcept nogil:
    _unified_socket_write_impl(fd)


def _handle_callback_wrapper(CallbackWrapper callback_wrapper, int success):
    CallbackWrapper.functor_run(callback_wrapper.c_functor(), success)


def _close_socket(object socket):
    if socket is None:
        return
    try:
        socket.close()
    except Exception:  # pylint: disable=broad-except
        # no-op; the socket module is already torn down
        pass


cdef class BaseCompletionQueue:

    cdef grpc_completion_queue* c_ptr(self):
        return self._cq


cdef class _BoundEventLoop:
    """One event loop's connection to the poller thread.

    Owns the loop's private wake-up socketpair and its mailbox. The poller
    pushes the loop's events on the mailbox and writes a byte; the loop's
    reader callback drains the mailbox and completes events inline. No event
    is ever scheduled onto a loop from another thread.
    """
    def __cinit__(self, object loop):
        self.loop = loop
        self._read_socket, self._write_socket = socket.socketpair()
        self._mailbox.write_fd = self._write_socket.fileno()
        self._loop_owning_thread_id = threading.get_ident()

        # read_socket: non-blocking so a false wake-up causes a
        # `BlockingIOError` rather than a hang
        self._read_socket.setblocking(False)

        # write_socket: the poller thread must never block in write() when the
        # buffer is full (this loop may not be running); a full buffer already
        # guarantees a pending wake-up
        self._write_socket.setblocking(False)

        # NOTE(lidiz) There isn't a way to cleanly pre-check if fd monitoring
        # support is available or not. Checking the event loop policy is not
        # good enough. The application can have its own loop implementation, or
        # uses different types of event loops (e.g., 1 Proactor, 3 Selectors).
        if _has_fd_monitoring.load():
            try:
                self.loop.add_reader(self._read_socket, self._handle_events)
                self._has_reader = True
            except NotImplementedError:
                _has_fd_monitoring.store(False)
                self._has_reader = False

    def __dealloc__(self):
        # reached once nothing refers to this binding anymore
        self._drain_queue(False)
        _close_socket(self._read_socket)
        _close_socket(self._write_socket)

    def _handle_events(self):
        """Reader callback; runs on the loop's thread when the socket wakes.

        The socket read MUST come before the `_drain_queue` call due to the fact
        that poller thread writes a wake-up byte only when it pushes into an
        empty mailbox. Otherwise we could end-up with stalled loop.
        """
        cdef bytes data = None
        try:
            # reading a large chunk collapses all pending wake-ups into one
            # callback. Each callback will drain the entire mailbox's queue
            # anyway, so the chunk size does not influence correctness. It
            # should be just "large enough"
            data = self._read_socket.recv(_WAKEUP_READ_SIZE)
        except (BlockingIOError, InterruptedError):
            pass

        if data is not None and not data:
            # EOF marker found: the queue was torn down and closed its end of
            # socket
            self.close_read_socket()
            return

        self._drain_queue(True)

    cdef _drain_queue(self, bint complete_futures):
        """Empties the mailbox: completes the futures, or just releases them.

        When `complete_futures` is set the events are processed normally. This
        mode requires the loop's own thread and an open loop. Otherwise futures
        are released and `CallbackWrapper` references are decremented to balance
        the `CallbackWrapper__cinit__` call.
        """
        cdef cpp_event_queue pending_events
        cdef grpc_event event
        cdef CallbackContext *context

        self._mailbox.mtx.lock()
        pending_events.swap(self._mailbox.events)
        self._mailbox.mtx.unlock()

        while not pending_events.empty():
            event = pending_events.front()
            pending_events.pop()

            if complete_futures:
                CallbackWrapper.functor_run(
                    <grpc_completion_queue_functor *>event.tag,
                    event.success
                )
            else:
                context = <CallbackContext *>event.tag
                cpython.Py_DECREF(<object>context.callback_wrapper)

    cdef close_read_socket(self):
        """Closes the reader socket and drains pending events.

        Must run on the loop's owner thread. On a closed loop `remove_reader`
        is a no-op and pending events can be only released.
        """
        if self._has_reader:
            self._has_reader = False
            try:
                self.loop.remove_reader(self._read_socket)
            except (KeyError, ValueError, RuntimeError) as exc:
                _LOGGER.debug(
                    f"Failed to remove reader from {self.loop}: {exc}"
                )

        self._drain_queue(not self.loop.is_closed())
        _close_socket(self._read_socket)
        self._read_socket = None

    cdef close_write_socket(self):
        """Closes the write end of the socket.

        Should be executed once poller thread was joined. The read end then
        reports EOF, so the loop's registered reader unregistered itself next
        time the loop runs, on the loop's own thread.
        """
        _close_socket(self._write_socket)
        self._write_socket = None


cdef class PollerCompletionQueue(BaseCompletionQueue):

    def __cinit__(self):
        self._cq = grpc_completion_queue_create_for_next(NULL)
        self._shutdown.store(False)
        self._poller_thread = threading.Thread(
            target=self._poll_wrapper, name="grpc-aio-poller", daemon=True
        )
        self._poller_thread.start()
        self._loops = {}

    def bind_loop(self, object loop):
        cdef _BoundEventLoop bound
        if loop in self._loops:
            return
        else:
            self._sweep_closed_loops()
            bound = _BoundEventLoop(loop)
            self._loops[loop] = bound
            self._mailboxes_mutex.lock()
            self._mailboxes[<size_t>(<void *>loop)] = &bound._mailbox
            self._mailboxes_mutex.unlock()

    cdef _sweep_closed_loops(self):
        cdef _BoundEventLoop bound
        cdef list closed_loops = [l for l in self._loops if l.is_closed()]

        for loop in closed_loops:
            bound = self._loops.pop(loop)
            self._mailboxes_mutex.lock()
            self._mailboxes.erase(<size_t>(<void *>loop))
            self._mailboxes_mutex.unlock()
            try:
                bound.close_write_socket()
                bound.close_read_socket()
            except Exception as exc:  # pylint: disable=broad-except
                _LOGGER.debug(
                    f"Failed to unbind closed loop {loop}: {exc}"
                )

    cdef int _poll(self) except -1 nogil:
        cdef grpc_event event

        while not self._shutdown.load():
            event = grpc_completion_queue_next(self._cq,
                                               _GPR_INF_FUTURE,
                                               NULL)

            if event.type == GRPC_QUEUE_TIMEOUT:
                with gil:
                    raise AssertionError("Core should not return GRPC_QUEUE_TIMEOUT!")
            elif event.type == GRPC_QUEUE_SHUTDOWN:
                self._shutdown.store(True)
                break
            else:
                self._dispatch(event)

    cdef void _dispatch(self, grpc_event event) noexcept nogil:
        """Dispatches one Core grpc_event to the loop that owns it.

        Routing is done via loop's mailbox and wake-up socket if it has one;
        else the poller thread itself hands the event over.
        """
        cdef CallbackContext *context = <CallbackContext *>event.tag
        cdef _LoopMailbox *mailbox = NULL
        cdef size_t key
        cdef bint was_empty

        if _has_fd_monitoring.load():
            key = <size_t>(<void *>context.loop)
            self._mailboxes_mutex.lock()
            if self._mailboxes.count(key):
                mailbox = self._mailboxes[key]
                mailbox.mtx.lock()
                was_empty = mailbox.events.empty()
                mailbox.events.push(event)
                mailbox.mtx.unlock()
                # wake-up the loop on the empty -> non-empty transition
                if was_empty:
                    _unified_socket_write(mailbox.write_fd)
            self._mailboxes_mutex.unlock()
        if mailbox == NULL:
            # no mailbox for this loop (it was never bound, it was closed and
            # swept or loops do not support file descriptors at all); the poller
            # thread delegates the event to the loop owning thread
            with gil:
                try:
                    (<object>context.loop).call_soon_threadsafe(
                        _handle_callback_wrapper,
                        <CallbackWrapper>context.callback_wrapper,
                        event.success
                    )
                except RuntimeError:
                    # the loop is closed; release Core's reference of
                    # the wrapper
                    cpython.Py_DECREF(<object>context.callback_wrapper)

    def _poll_wrapper(self):
        with nogil:
            self._poll()

    cdef shutdown(self):
        """Tears down the core completion queue"""
        # TODO(https://github.com/grpc/grpc/issues/22365) perform graceful shutdown
        with nogil:
            grpc_completion_queue_shutdown(self._cq)

        # Without fd monitoring the poller thread itself dispatches events and
        # may reach here through a dealloc chain. Raising shutdown flag makes
        # `_poll` method exit.
        if self._poller_thread is threading.current_thread():
            self._shutdown.store(True)
        else:
            while self._poller_thread.is_alive():
                self._poller_thread.join(timeout=_POLL_AWAKE_INTERVAL_S)

        # drain the remaining events until shutdown event is encountered
        while True:
            with nogil:
                event = grpc_completion_queue_next(self._cq,
                                                   _GPR_INF_FUTURE,
                                                   NULL)

            if event.type == GRPC_QUEUE_SHUTDOWN:
                break
            if event.type != GRPC_QUEUE_TIMEOUT:
                self._dispatch(event)

        grpc_completion_queue_destroy(self._cq)
        self._cq = NULL

        self._mailboxes_mutex.lock()
        self._mailboxes.clear()
        self._mailboxes_mutex.unlock()

        self._unbind_loops()

    cdef _unbind_loops(self):
        cdef dict loops = self._loops
        cdef _BoundEventLoop bound
        cdef object running_loop = _get_running_loop()
        cdef object thread_id = threading.get_ident()

        self._loops = {}

        for bound in loops.values():
            try:
                bound.close_write_socket()
                if bound.loop is running_loop or (
                        thread_id == bound._loop_owning_thread_id
                        and not bound.loop.is_running()):
                    bound.close_read_socket()
            except Exception as exc:  # pylint: disable=broad-except
                _LOGGER.debug(
                    f"Failed to unbind {bound.loop} from the poller: {exc}"
                )
