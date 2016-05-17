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


cdef class Channel:

  def __cinit__(self, target, ChannelArgs arguments=None,
                ChannelCredentials channel_credentials=None):
    cdef grpc_channel_args *c_arguments = NULL
    cdef char *c_target = NULL
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
    c_target = target
    if channel_credentials is None:
      with nogil:
        self.c_channel = grpc_insecure_channel_create(c_target, c_arguments,
                                                      NULL)
    else:
      with nogil:
        self.c_channel = grpc_secure_channel_create(
            channel_credentials.c_credentials, c_target, c_arguments, NULL)
      self.references.append(channel_credentials)
    self.references.append(target)
    self.references.append(arguments)

  def create_call(self, Call parent, int flags,
                  CompletionQueue queue not None,
                  method, host, Timespec deadline not None):
    if queue.is_shutting_down:
      raise ValueError("queue must not be shutting down or shutdown")
    if isinstance(method, bytes):
      pass
    elif isinstance(method, basestring):
      method = method.encode()
    else:
      raise TypeError("expected method to be str or bytes")
    cdef char *method_c_string = method
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
    cdef Call operation_call = Call()
    operation_call.references = [self, method, host, queue]
    cdef grpc_call *parent_call = NULL
    if parent is not None:
      parent_call = parent.c_call
    with nogil:
      operation_call.c_call = grpc_channel_create_call(
          self.c_channel, parent_call, flags,
          queue.c_completion_queue, method_c_string, host_c_string,
          deadline.c_time, NULL)
    return operation_call

  def check_connectivity_state(self, bint try_to_connect):
    cdef grpc_connectivity_state result
    with nogil:
      result = grpc_channel_check_connectivity_state(self.c_channel,
                                                     try_to_connect)
    return result

  def watch_connectivity_state(
      self, grpc_connectivity_state last_observed_state,
      Timespec deadline not None, CompletionQueue queue not None, tag):
    cdef OperationTag operation_tag = OperationTag(tag)
    cpython.Py_INCREF(operation_tag)
    with nogil:
      grpc_channel_watch_connectivity_state(
          self.c_channel, last_observed_state, deadline.c_time,
          queue.c_completion_queue, <cpython.PyObject *>operation_tag)

  def target(self):
    cdef char *target = NULL
    with nogil:
      target = grpc_channel_get_target(self.c_channel)
    result = <bytes>target
    with nogil:
      gpr_free(target)
    return result

  def __dealloc__(self):
    if self.c_channel != NULL:
      with nogil:
        grpc_channel_destroy(self.c_channel)
