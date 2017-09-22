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

cimport cpython

import time

cdef grpc_get_server_credentials_result _get_server_credentials_cb_wrapper(
        grpc_server_credentials **creds, void* cb_arg) with gil:
  """
  Args:
    cb_arg (callable): Callback that takes no arguments and should return
      a (bool, grpc.ServerCredentials) tuple, where the bool specifies if
      the returned credentials are new and should be used.

  We are not catching any exception here, because cython will happily catch
  and ignore it, and will log for us, and also the core lib will continue
  as if the credentials have not changed, which is a reasonable behavior.
  """
  # this should be a grpc._cython._cygrpc.credentials.ServerCredentials
  cdef ServerCredentials server_creds = None
  if not cb_arg:
    raise ValueError('internal error: cb_arg must be specified')
  user_cb = <object>cb_arg
  is_new, server_creds_wrapper = user_cb()
  if not is_new:
    return GRPC_GET_SERVER_CREDENTIALS_UNCHANGED
  # TODO: perhaps make sure user gives us the correct stuff?
  # server_creds_wrapper should be a grpc.ServerCredentials, e.g.,
  # one returned by grpc.ssl_server_credentials()
  server_creds = server_creds_wrapper._credentials
  creds[0] = <grpc_server_credentials*>server_creds.c_credentials
  # now the c-core lib owns the memory so we forget about it here
  server_creds.c_credentials = NULL
  return GRPC_GET_SERVER_CREDENTIALS_NEW


cdef class Server:

  def __cinit__(self, ChannelArgs arguments):
    grpc_init()
    cdef grpc_channel_args *c_arguments = NULL
    self.references = []
    self.registered_completion_queues = []
    if len(arguments) > 0:
      c_arguments = &arguments.c_args
      self.references.append(arguments)
    with nogil:
      self.c_server = grpc_server_create(c_arguments, NULL)
    self.is_started = False
    self.is_shutting_down = False
    self.is_shutdown = False

  def request_call(
      self, CompletionQueue call_queue not None,
      CompletionQueue server_queue not None, tag):
    if not self.is_started or self.is_shutting_down:
      raise ValueError("server must be started and not shutting down")
    if server_queue not in self.registered_completion_queues:
      raise ValueError("server_queue must be a registered completion queue")
    cdef grpc_call_error result
    cdef OperationTag operation_tag = OperationTag(tag)
    operation_tag.operation_call = Call()
    operation_tag.request_call_details = CallDetails()
    operation_tag.request_metadata = MetadataArray()
    operation_tag.references.extend([self, call_queue, server_queue])
    operation_tag.is_new_request = True
    operation_tag.batch_operations = Operations([])
    cpython.Py_INCREF(operation_tag)
    with nogil:
      result = grpc_server_request_call(
          self.c_server, &operation_tag.operation_call.c_call,
          &operation_tag.request_call_details.c_details,
          &operation_tag.request_metadata.c_metadata_array,
          call_queue.c_completion_queue, server_queue.c_completion_queue,
          <cpython.PyObject *>operation_tag)
    return result

  def register_completion_queue(
      self, CompletionQueue queue not None):
    if self.is_started:
      raise ValueError("cannot register completion queues after start")
    with nogil:
      grpc_server_register_completion_queue(
          self.c_server, queue.c_completion_queue, NULL)
    self.registered_completion_queues.append(queue)

  def start(self):
    if self.is_started:
      raise ValueError("the server has already started")
    self.backup_shutdown_queue = CompletionQueue(shutdown_cq=True)
    self.register_completion_queue(self.backup_shutdown_queue)
    self.is_started = True
    with nogil:
      grpc_server_start(self.c_server)
    # Ensure the core has gotten a chance to do the start-up work
    self.backup_shutdown_queue.poll(Timespec(None))

  def add_http2_port(self, bytes address,
                     ServerCredentials server_credentials=None):
    address = str_to_bytes(address)
    self.references.append(address)
    cdef int result
    cdef char *address_c_string = address
    if server_credentials is not None:
      self.references.append(server_credentials)
      with nogil:
        result = grpc_server_add_secure_http2_port(
            self.c_server, address_c_string, server_credentials.c_credentials)
    else:
      with nogil:
        result = grpc_server_add_insecure_http2_port(self.c_server,
                                                     address_c_string)
    return result

  cdef _c_shutdown(self, CompletionQueue queue, tag):
    self.is_shutting_down = True
    operation_tag = OperationTag(tag)
    operation_tag.shutting_down_server = self
    cpython.Py_INCREF(operation_tag)
    with nogil:
      grpc_server_shutdown_and_notify(
          self.c_server, queue.c_completion_queue,
          <cpython.PyObject *>operation_tag)

  def shutdown(self, CompletionQueue queue not None, tag):
    cdef OperationTag operation_tag
    if queue.is_shutting_down:
      raise ValueError("queue must be live")
    elif not self.is_started:
      raise ValueError("the server hasn't started yet")
    elif self.is_shutting_down:
      return
    elif queue not in self.registered_completion_queues:
      raise ValueError("expected registered completion queue")
    else:
      self._c_shutdown(queue, tag)

  cdef notify_shutdown_complete(self):
    # called only by a completion queue on receiving our shutdown operation tag
    self.is_shutdown = True

  def cancel_all_calls(self):
    if not self.is_shutting_down:
      raise RuntimeError("the server must be shutting down to cancel all calls")
    elif self.is_shutdown:
      return
    else:
      with nogil:
        grpc_server_cancel_all_calls(self.c_server)

  def __dealloc__(self):
    if self.c_server != NULL:
      if not self.is_started:
        pass
      elif self.is_shutdown:
        pass
      elif not self.is_shutting_down:
        # the user didn't call shutdown - use our backup queue
        self._c_shutdown(self.backup_shutdown_queue, None)
        # and now we wait
        while not self.is_shutdown:
          self.backup_shutdown_queue.poll()
      else:
        # We're in the process of shutting down, but have not shutdown; can't do
        # much but repeatedly release the GIL and wait
        while not self.is_shutdown:
          time.sleep(0)
      grpc_server_destroy(self.c_server)
    grpc_shutdown()
