# Copyright 2015 gRPC authors.
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

cimport cpython

import logging
import os
import sys
import threading
import time

import grpc

try:
    import asyncio
except ImportError:
    # TODO(https://github.com/grpc/grpc/issues/19728) Improve how Aio Cython is
    # distributed without breaking none compatible Python versions. For now, if
    # Asyncio package is not available we just skip it.
    pass

# The only copy of Python logger for the Cython extension
_LOGGER = logging.getLogger(__name__)

# TODO(atash): figure out why the coverage tool gets confused about the Cython
# coverage plugin when the following files don't have a '.pxi' suffix.
include "_cygrpc/grpc_string.pyx.pxi"
include "_cygrpc/arguments.pyx.pxi"
include "_cygrpc/call.pyx.pxi"
include "_cygrpc/channel.pyx.pxi"
include "_cygrpc/channelz.pyx.pxi"
include "_cygrpc/csds.pyx.pxi"
include "_cygrpc/credentials.pyx.pxi"
include "_cygrpc/completion_queue.pyx.pxi"
include "_cygrpc/event.pyx.pxi"
include "_cygrpc/metadata.pyx.pxi"
include "_cygrpc/operation.pyx.pxi"
include "_cygrpc/propagation_bits.pyx.pxi"
include "_cygrpc/records.pyx.pxi"
include "_cygrpc/security.pyx.pxi"
include "_cygrpc/server.pyx.pxi"
include "_cygrpc/tag.pyx.pxi"
include "_cygrpc/time.pyx.pxi"
include "_cygrpc/vtable.pyx.pxi"
include "_cygrpc/_hooks.pyx.pxi"

include "_cygrpc/grpc_gevent.pyx.pxi"

include "_cygrpc/thread.pyx.pxi"

IF UNAME_SYSNAME == "Windows":
    include "_cygrpc/fork_windows.pyx.pxi"
ELSE:
    include "_cygrpc/fork_posix.pyx.pxi"

# Following pxi files are part of the Aio module
include "_cygrpc/aio/common.pyx.pxi"
include "_cygrpc/aio/rpc_status.pyx.pxi"
include "_cygrpc/aio/completion_queue.pyx.pxi"
include "_cygrpc/aio/callback_common.pyx.pxi"
include "_cygrpc/aio/grpc_aio.pyx.pxi"
include "_cygrpc/aio/call.pyx.pxi"
include "_cygrpc/aio/channel.pyx.pxi"
include "_cygrpc/aio/server.pyx.pxi"


#
# initialize gRPC
#
cdef extern from "Python.h":

  int PyEval_InitThreads()

cdef _initialize():
  # We have Python callbacks called by c-core threads, this ensures the GIL
  # is initialized.
  PyEval_InitThreads()
  grpc_set_ssl_roots_override_callback(
          <grpc_ssl_roots_override_callback>ssl_roots_override_callback)


_initialize()
