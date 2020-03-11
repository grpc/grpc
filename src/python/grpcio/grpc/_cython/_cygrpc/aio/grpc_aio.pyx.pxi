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


cdef bint _grpc_aio_initialized = False
# NOTE(lidiz) Theoretically, applications can run in multiple event loops as
# long as they are in the same thread with same magic. However, I don't think
# we should support this use case. So, the gRPC Python Async Stack should use
# a single event loop picked by "init_grpc_aio".
cdef object _grpc_aio_loop


def init_grpc_aio():
    global _grpc_aio_initialized
    global _grpc_aio_loop

    if _grpc_aio_initialized:
        return
    else:
        _grpc_aio_initialized = True

    # Anchors the event loop that the gRPC library going to use.
    _grpc_aio_loop = asyncio.get_event_loop()

    # Activates asyncio IO manager
    install_asyncio_iomgr()

    # TODO(https://github.com/grpc/grpc/issues/22244) we need a the
    # grpc_shutdown_blocking() counterpart for this call. Otherwise, the gRPC
    # library won't shutdown cleanly.
    grpc_init()

    # Timers are triggered by the Asyncio loop. We disable
    # the background thread that is being used by the native
    # gRPC iomgr.
    grpc_timer_manager_set_threading(False)

    # gRPC callbaks are executed within the same thread used by the Asyncio
    # event loop, as it is being done by the other Asyncio callbacks.
    Executor.SetThreadingAll(False)

    _grpc_aio_initialized = False


def grpc_aio_loop():
    """Returns the one-and-only gRPC Aio event loop."""
    return _grpc_aio_loop
