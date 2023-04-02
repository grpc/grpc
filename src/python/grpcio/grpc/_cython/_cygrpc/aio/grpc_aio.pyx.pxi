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

import enum

cdef str _GRPC_ASYNCIO_ENGINE = os.environ.get('GRPC_ASYNCIO_ENGINE', 'poller').upper()
cdef _AioState _global_aio_state = _AioState()


class AsyncIOEngine(enum.Enum):
    # NOTE(lidiz) the support for custom_io_manager is removed in favor of the
    # EventEngine project, which will be the only IO platform in Core.
    CUSTOM_IO_MANAGER = 'custom_io_manager'
    POLLER = 'poller'


cdef _default_asyncio_engine():
    return AsyncIOEngine.POLLER


cdef grpc_completion_queue *global_completion_queue():
    return _global_aio_state.cq.c_ptr()


cdef class _AioState:

    def __cinit__(self):
        self.lock = threading.RLock()
        self.refcount = 0
        self.engine = None
        self.cq = None


cdef _initialize_poller():
    # Initializes gRPC Core, must be called before other Core API
    grpc_init()

    # Creates the only completion queue
    _global_aio_state.cq = PollerCompletionQueue()


cdef _actual_aio_initialization():
    # Picks the engine for gRPC AsyncIO Stack
    _global_aio_state.engine = AsyncIOEngine.__members__.get(
        _GRPC_ASYNCIO_ENGINE,
        _default_asyncio_engine(),
    )
    _LOGGER.debug('Using %s as I/O engine', _global_aio_state.engine)

    # Initializes the process-level state accordingly
    if _global_aio_state.engine is AsyncIOEngine.POLLER:
        _initialize_poller()
    else:
        raise ValueError('Unsupported engine type [%s]' % _global_aio_state.engine)


def _grpc_shutdown_wrapper(_):
    """A thin Python wrapper of Core's shutdown function.

    Define functions are not allowed in "cdef" functions, and Cython complains
    about a simple lambda with a C function.
    """
    grpc_shutdown()


cdef _actual_aio_shutdown():
    if _global_aio_state.engine is AsyncIOEngine.POLLER:
        (<PollerCompletionQueue>_global_aio_state.cq).shutdown()
        grpc_shutdown()
    else:
        raise ValueError('Unsupported engine type [%s]' % _global_aio_state.engine)


cdef _initialize_per_loop():
    cdef object loop = get_working_loop()
    if _global_aio_state.engine is AsyncIOEngine.POLLER:
        _global_aio_state.cq.bind_loop(loop)


cpdef init_grpc_aio():
    """Initializes the gRPC AsyncIO module.

    Expected to be invoked on critical class constructors.
    E.g., AioChannel, AioServer.
    """
    with _global_aio_state.lock:
        _global_aio_state.refcount += 1
        if _global_aio_state.refcount == 1:
            _actual_aio_initialization()
        _initialize_per_loop()


cpdef shutdown_grpc_aio():
    """Shuts down the gRPC AsyncIO module.

    Expected to be invoked on critical class destructors.
    E.g., AioChannel, AioServer.
    """
    with _global_aio_state.lock:
        assert _global_aio_state.refcount > 0
        _global_aio_state.refcount -= 1
        if not _global_aio_state.refcount:
            _actual_aio_shutdown()
