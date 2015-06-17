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

from grpc._cython._cygrpc cimport grpc
from grpc._cython._cygrpc cimport call
from grpc._cython._cygrpc cimport channel
from grpc._cython._cygrpc cimport credentials
from grpc._cython._cygrpc cimport completion_queue
from grpc._cython._cygrpc cimport records
from grpc._cython._cygrpc cimport server

from grpc._cython._cygrpc import call
from grpc._cython._cygrpc import channel
from grpc._cython._cygrpc import credentials
from grpc._cython._cygrpc import completion_queue
from grpc._cython._cygrpc import records
from grpc._cython._cygrpc import server

StatusCode = records.StatusCode
CallError = records.CallError
Timespec = records.Timespec
CallDetails = records.CallDetails
Event = records.Event
ByteBuffer = records.ByteBuffer
SslPemKeyCertPair = records.SslPemKeyCertPair
ChannelArg = records.ChannelArg
ChannelArgs = records.ChannelArgs
Metadatum = records.Metadatum
Metadata = records.Metadata
Operation = records.Operation
Operations = records.Operations

ClientCredentials = credentials.ClientCredentials
ServerCredentials = credentials.ServerCredentials

CompletionQueue = completion_queue.CompletionQueue
Channel = channel.Channel
Server = server.Server
Call = call.Call


#
# Global state
#

cdef class _ModuleState:

  def __cinit__(self):
    grpc.grpc_init()

  def __dealloc__(self):
    grpc.grpc_shutdown()

_module_state = _ModuleState()

