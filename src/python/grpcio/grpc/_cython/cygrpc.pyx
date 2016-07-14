# Copyright 2015, Google Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

cimport cpython

import pkg_resources
import os.path
import sys

# TODO(atash): figure out why the coverage tool gets confused about the Cython
# coverage plugin when the following files don't have a '.pxi' suffix.
include "grpc/_cython/_cygrpc/grpc_string.pyx.pxi"
include "grpc/_cython/_cygrpc/call.pyx.pxi"
include "grpc/_cython/_cygrpc/channel.pyx.pxi"
include "grpc/_cython/_cygrpc/credentials.pyx.pxi"
include "grpc/_cython/_cygrpc/completion_queue.pyx.pxi"
include "grpc/_cython/_cygrpc/records.pyx.pxi"
include "grpc/_cython/_cygrpc/security.pyx.pxi"
include "grpc/_cython/_cygrpc/server.pyx.pxi"

#
# initialize gRPC
#


cdef extern from "Python.h":

  int Py_AtExit(void(*func)())


def _initialize():
  grpc_init()
  grpc_set_ssl_roots_override_callback(
          <grpc_ssl_roots_override_callback>ssl_roots_override_callback)

  if Py_AtExit(grpc_shutdown) != 0:
    raise ImportError('failed to register gRPC library shutdown callbacks')


_initialize()
