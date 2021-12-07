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
# distutils: language=c++

from libc cimport string
from cython.operator cimport dereference
from python_ref cimport Py_INCREF, Py_DECREF
from libc.stdio cimport printf

import atexit
import errno
import sys
gevent_g = None
gevent_socket = None
gevent_hub = None
gevent_event = None
g_event = None
g_pool = None
g_gevent_threadpool = None


cdef queue[void*] g_greenlets_to_run
cdef condition_variable g_greenlets_cv
cdef mutex g_greenlets_mu
cdef bint g_shutdown_greenlets_to_run_queue = False


cdef _submit_to_greenlet_queue(object to_call):
  cdef unique_lock[mutex]* lk
  Py_INCREF(to_call)
  with nogil:
    lk = new unique_lock[mutex](g_greenlets_mu)
    g_greenlets_to_run.push(<void*>(to_call))
    del lk
    g_greenlets_cv.notify_all()


# TODO: This wrapper is not necessary.
def _spawn_greenlet(*args):
  _submit_to_greenlet_queue(args)


cdef object await_next_greenlet():
  cdef unique_lock[mutex]* lk
  with nogil:
    # Cython doesn't allow us to do proper stack allocations, so we can't take
    # advantage of RAII.
    lk = new unique_lock[mutex](g_greenlets_mu)
    while not g_shutdown_greenlets_to_run_queue:
      if not g_greenlets_to_run.empty():
        break
      g_greenlets_cv.wait(dereference(lk))
  cdef object to_call = <object>g_greenlets_to_run.front()
  Py_DECREF(to_call)
  g_greenlets_to_run.pop()
  del lk
  return to_call

def spawn_greenlets():
  while True:
    to_call = g_gevent_threadpool.apply(await_next_greenlet, ())
    fn = to_call[0]
    args = to_call[1:]
    fn(*args)

def shutdown_await_next_greenlet():
  # TODO: Is this global statement necessary?
  global g_shutdown_greenlets_to_run_queue
  cdef unique_lock[mutex]* lk
  with nogil:
    lk = new unique_lock[mutex](g_greenlets_mu)
    g_shutdown_greenlets_to_run_queue = True
  del lk
  g_greenlets_cv.notify_all()

def init_grpc_gevent():
  # Lazily import gevent
  global gevent_hub
  global g_gevent_threadpool
  global g_pool

  import gevent
  gevent_hub = gevent.hub
  import gevent.pool

  g_gevent_threadpool = gevent_hub.get_hub().threadpool

  # TODO: Move this to test runner.
  g_gevent_threadpool.maxsize = 1024
  g_gevent_threadpool.size = 32

  g_pool = gevent.pool.Group()

  g_pool.spawn(spawn_greenlets)

  def cb_func(cb, args):
    _spawn_greenlet(cb, *args)
  set_async_callback_func(cb_func)

  # TODO: Document how this all works.
  atexit.register(shutdown_await_next_greenlet)
