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
"""Interpreter-exit aware daemon thread support"""


import atexit
import threading
import weakref


INTERPRETER_EXIT_CHECK_PERIOD_S = 1.0

_daemon_threads = weakref.WeakSet()
_exiting = False
_shutdown_lock = threading.Lock()


def _await_daemon_threads():
    global _exiting
    _exiting = True
    daemon_threads = list(_daemon_threads)
    for t in daemon_threads:
        t.join()

def add_daemon_thread(daemon_thread):
    with _shutdown_lock:
        _daemon_threads.add(daemon_thread)

def is_exiting():
    return _exiting


atexit.register(_await_daemon_threads)
