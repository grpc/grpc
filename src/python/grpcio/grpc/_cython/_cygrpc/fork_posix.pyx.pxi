# Copyright 2018 gRPC authors.
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


import logging
import os
import threading

_LOGGER = logging.getLogger(__name__)

_AWAIT_THREADS_TIMEOUT_SECONDS = 5

_TRUE_VALUES = ['yes',  'Yes',  'YES', 'true', 'True', 'TRUE', '1']

# This flag enables experimental support within gRPC Python for applications
# that will fork() without exec(). When enabled, gRPC Python will attempt to
# pause all of its internally created threads before the fork syscall proceeds.
#
# For this to be successful, the application must not have multiple threads of
# its own calling into gRPC when fork is invoked. Any callbacks from gRPC
# Python-spawned threads into user code (e.g., callbacks for asynchronous RPCs)
# must  not block and should execute quickly.
#
# This flag is not supported on Windows.
_GRPC_ENABLE_FORK_SUPPORT = (
    os.environ.get('GRPC_ENABLE_FORK_SUPPORT', '0')
        .lower() in _TRUE_VALUES)

_GRPC_POLL_STRATEGY = os.environ.get('GRPC_POLL_STRATEGY')

cdef void __prefork() nogil:
    with gil:
        with _fork_state.fork_in_progress_condition:
            _fork_state.fork_in_progress = True
        if not _fork_state.active_thread_count.await_zero_threads(
                _AWAIT_THREADS_TIMEOUT_SECONDS):
            _LOGGER.error(
                'Failed to shutdown gRPC Python threads prior to fork. '
                'Behavior after fork will be undefined.')


cdef void __postfork_parent() nogil:
    with gil:
        with _fork_state.fork_in_progress_condition:
            _fork_state.post_fork_child_cleanup_callbacks = []
            _fork_state.fork_in_progress = False
            _fork_state.fork_in_progress_condition.notify_all()


cdef void __postfork_child() nogil:
    with gil:
        with _fork_state.fork_in_progress_condition:
            for state_to_reset in _fork_state.postfork_states_to_reset:
                state_to_reset.reset_postfork_child()
            _fork_state.fork_epoch += 1
            _fork_state.post_fork_child_cleanup_callbacks = []
            _fork_state.fork_in_progress = False


def fork_handlers_and_grpc_init():
    grpc_init()
    if _GRPC_ENABLE_FORK_SUPPORT:
        # TODO(ericgribkoff) epoll1 is default for grpcio distribution. Decide whether to expose
        # grpc_get_poll_strategy_name() from ev_posix.cc to get actual polling choice.
        if _GRPC_POLL_STRATEGY is not None and _GRPC_POLL_STRATEGY != "epoll1":
            _LOGGER.error(
                'gRPC Python fork support is only compatible with the epoll1 '
                'polling engine')
            return
        with _fork_state.fork_handler_registered_lock:
            if not _fork_state.fork_handler_registered:
                pthread_atfork(&__prefork, &__postfork_parent, &__postfork_child)
                _fork_state.fork_handler_registered = True


class ForkManagedThread(object):
    def __init__(self, target, args=()):
        if _GRPC_ENABLE_FORK_SUPPORT:
            def managed_target(*args):
                try:
                    target(*args)
                finally:
                    _fork_state.active_thread_count.decrement()
            self._thread = threading.Thread(target=managed_target, args=args)
        else:
            self._thread = threading.Thread(target=target, args=args)

    def setDaemon(self, daemonic):
        self._thread.daemon = daemonic

    def start(self):
        if _GRPC_ENABLE_FORK_SUPPORT:
            _fork_state.active_thread_count.increment()
        self._thread.start()

    def join(self):
        self._thread.join()


def block_if_fork_in_progress(postfork_state_to_reset=None):
    if _GRPC_ENABLE_FORK_SUPPORT:
        with _fork_state.fork_in_progress_condition:
            if not _fork_state.fork_in_progress:
                return
            if postfork_state_to_reset is not None:
                _fork_state.postfork_states_to_reset.append(postfork_state_to_reset)
            _fork_state.active_thread_count.decrement()
            _fork_state.fork_in_progress_condition.wait()
            _fork_state.active_thread_count.increment()


def enter_user_request_generator():
    if _GRPC_ENABLE_FORK_SUPPORT:
        _fork_state.active_thread_count.decrement()


def return_from_user_request_generator():
    if _GRPC_ENABLE_FORK_SUPPORT:
        _fork_state.active_thread_count.increment()


def get_fork_epoch():
    return _fork_state.fork_epoch


def is_fork_support_enabled():
    return _GRPC_ENABLE_FORK_SUPPORT


class _ActiveThreadCount(object):
    def __init__(self):
        self._num_active_threads = 0
        self._condition = threading.Condition()

    def increment(self):
        with self._condition:
            self._num_active_threads += 1

    def decrement(self):
        with self._condition:
            self._num_active_threads -= 1
            if self._num_active_threads == 0:
                self._condition.notify_all()

    def await_zero_threads(self, timeout_secs):
        with self._condition:
            if self._num_active_threads > 0:
                self._condition.wait(timeout_secs)
            return self._num_active_threads == 0


class _ForkState(object):
    def __init__(self):
        self.fork_in_progress_condition = threading.Condition()
        self.fork_in_progress = False
        self.postfork_states_to_reset = []
        self.fork_handler_registered_lock = threading.Lock()
        self.fork_handler_registered = False
        self.active_thread_count = _ActiveThreadCount()
        self.fork_epoch = 0


_fork_state = _ForkState()
