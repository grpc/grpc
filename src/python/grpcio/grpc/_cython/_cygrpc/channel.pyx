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

from grpc._cython._cygrpc cimport call
from grpc._cython._cygrpc cimport completion_queue
from grpc._cython._cygrpc cimport credentials
from grpc._cython._cygrpc cimport grpc
from grpc._cython._cygrpc cimport records


cdef class Channel:

  def __cinit__(self, target, records.ChannelArgs arguments=None,
                credentials.ChannelCredentials channel_credentials=None):
    cdef grpc.grpc_channel_args *c_arguments = NULL
    self.c_channel = NULL
    self.references = []
    if arguments is not None:
      c_arguments = &arguments.c_args
    if isinstance(target, bytes):
      pass
    elif isinstance(target, basestring):
      target = target.encode()
    else:
      raise TypeError("expected target to be str or bytes")
    if channel_credentials is None:
      self.c_channel = grpc.grpc_insecure_channel_create(target, c_arguments,
                                                         NULL)
    else:
      self.c_channel = grpc.grpc_secure_channel_create(
          channel_credentials.c_credentials, target, c_arguments, NULL)
      self.references.append(channel_credentials)
    self.references.append(target)
    self.references.append(arguments)

  def create_call(self, call.Call parent, int flags,
                  completion_queue.CompletionQueue queue not None,
                  method, host, records.Timespec deadline not None):
    if queue.is_shutting_down:
      raise ValueError("queue must not be shutting down or shutdown")
    if isinstance(method, bytes):
      pass
    elif isinstance(method, basestring):
      method = method.encode()
    else:
      raise TypeError("expected method to be str or bytes")
    cdef char *host_c_string = NULL
    if host is None:
      pass
    elif isinstance(host, bytes):
      host_c_string = host
    elif isinstance(host, basestring):
      host = host.encode()
      host_c_string = host
    else:
      raise TypeError("expected host to be str, bytes, or None")
    cdef call.Call operation_call = call.Call()
    operation_call.references = [self, method, host, queue]
    cdef grpc.grpc_call *parent_call = NULL
    if parent is not None:
      parent_call = parent.c_call
    operation_call.c_call = grpc.grpc_channel_create_call(
        self.c_channel, parent_call, flags,
        queue.c_completion_queue, method, host_c_string, deadline.c_time,
        NULL)
    return operation_call

  def check_connectivity_state(self, bint try_to_connect):
    return grpc.grpc_channel_check_connectivity_state(self.c_channel,
                                                      try_to_connect)

  def watch_connectivity_state(
      self, last_observed_state, records.Timespec deadline not None,
      completion_queue.CompletionQueue queue not None, tag):
    cdef records.OperationTag operation_tag = records.OperationTag(tag)
    cpython.Py_INCREF(operation_tag)
    grpc.grpc_channel_watch_connectivity_state(
        self.c_channel, last_observed_state, deadline.c_time,
        queue.c_completion_queue, <cpython.PyObject *>operation_tag)

  def target(self):
    cdef char * target = grpc.grpc_channel_get_target(self.c_channel)
    result = <bytes>target
    grpc.gpr_free(target)
    return result

  def __dealloc__(self):
    if self.c_channel != NULL:
      grpc.grpc_channel_destroy(self.c_channel)
