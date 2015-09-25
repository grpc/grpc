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

import time


cdef class Server:

  def __cinit__(self, records.ChannelArgs arguments=None):
    cdef grpc.grpc_channel_args *c_arguments = NULL
    self.references = []
    self.registered_completion_queues = []
    if arguments is not None:
      c_arguments = &arguments.c_args
      self.references.append(arguments)
    self.c_server = grpc.grpc_server_create(c_arguments, NULL)
    self.is_started = False
    self.is_shutting_down = False
    self.is_shutdown = False

  def request_call(
      self, completion_queue.CompletionQueue call_queue not None,
      completion_queue.CompletionQueue server_queue not None, tag):
    if not self.is_started or self.is_shutting_down:
      raise ValueError("server must be started and not shutting down")
    if server_queue not in self.registered_completion_queues:
      raise ValueError("server_queue must be a registered completion queue")
    cdef records.OperationTag operation_tag = records.OperationTag(tag)
    operation_tag.operation_call = call.Call()
    operation_tag.request_call_details = records.CallDetails()
    operation_tag.request_metadata = records.Metadata([])
    operation_tag.references.extend([self, call_queue, server_queue])
    operation_tag.is_new_request = True
    operation_tag.batch_operations = records.Operations([])
    cpython.Py_INCREF(operation_tag)
    return grpc.grpc_server_request_call(
        self.c_server, &operation_tag.operation_call.c_call,
        &operation_tag.request_call_details.c_details,
        &operation_tag.request_metadata.c_metadata_array,
        call_queue.c_completion_queue, server_queue.c_completion_queue,
        <cpython.PyObject *>operation_tag)

  def register_completion_queue(
      self, completion_queue.CompletionQueue queue not None):
    if self.is_started:
      raise ValueError("cannot register completion queues after start")
    grpc.grpc_server_register_completion_queue(
        self.c_server, queue.c_completion_queue, NULL)
    self.registered_completion_queues.append(queue)

  def start(self):
    if self.is_started:
      raise ValueError("the server has already started")
    self.backup_shutdown_queue = completion_queue.CompletionQueue()
    self.register_completion_queue(self.backup_shutdown_queue)
    self.is_started = True
    grpc.grpc_server_start(self.c_server)

  def add_http2_port(self, address,
                     credentials.ServerCredentials server_credentials=None):
    if isinstance(address, bytes):
      pass
    elif isinstance(address, basestring):
      address = address.encode()
    else:
      raise TypeError("expected address to be a str or bytes")
    self.references.append(address)
    if server_credentials is not None:
      self.references.append(server_credentials)
      return grpc.grpc_server_add_secure_http2_port(
          self.c_server, address, server_credentials.c_credentials)
    else:
      return grpc.grpc_server_add_insecure_http2_port(self.c_server, address)

  def shutdown(self, completion_queue.CompletionQueue queue not None, tag):
    cdef records.OperationTag operation_tag
    if queue.is_shutting_down:
      raise ValueError("queue must be live")
    elif not self.is_started:
      raise ValueError("the server hasn't started yet")
    elif self.is_shutting_down:
      return
    elif queue not in self.registered_completion_queues:
      raise ValueError("expected registered completion queue")
    else:
      self.is_shutting_down = True
      operation_tag = records.OperationTag(tag)
      operation_tag.shutting_down_server = self
      operation_tag.references.extend([self, queue])
      cpython.Py_INCREF(operation_tag)
      grpc.grpc_server_shutdown_and_notify(
          self.c_server, queue.c_completion_queue,
          <cpython.PyObject *>operation_tag)

  cdef notify_shutdown_complete(self):
    # called only by a completion queue on receiving our shutdown operation tag
    self.is_shutdown = True

  def cancel_all_calls(self):
    if not self.is_shutting_down:
      raise ValueError("the server must be shutting down to cancel all calls")
    elif self.is_shutdown:
      return
    else:
      grpc.grpc_server_cancel_all_calls(self.c_server)

  def __dealloc__(self):
    if self.c_server != NULL:
      if not self.is_started:
        pass
      elif self.is_shutdown:
        pass
      elif not self.is_shutting_down:
        # the user didn't call shutdown - use our backup queue
        self.shutdown(self.backup_shutdown_queue, None)
        # and now we wait
        while not self.is_shutdown:
          self.backup_shutdown_queue.poll()
      else:
        # We're in the process of shutting down, but have not shutdown; can't do
        # much but repeatedly release the GIL and wait
        while not self.is_shutdown:
          time.sleep(0)
      grpc.grpc_server_destroy(self.c_server)

