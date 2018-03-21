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


cdef class Channel:

  def __cinit__(self, bytes target, object arguments,
                ChannelCredentials channel_credentials=None):
    grpc_init()
    self._vtable.copy = &_copy_pointer
    self._vtable.destroy = &_destroy_pointer
    self._vtable.cmp = &_compare_pointer
    cdef _ArgumentsProcessor arguments_processor = _ArgumentsProcessor(
        arguments)
    cdef grpc_channel_args *c_arguments = arguments_processor.c(&self._vtable)
    self.references = []
    c_target = target
    if channel_credentials is None:
      self.c_channel = grpc_insecure_channel_create(c_target, c_arguments, NULL)
    else:
      c_channel_credentials = channel_credentials.c()
      self.c_channel = grpc_secure_channel_create(
          c_channel_credentials, c_target, c_arguments, NULL)
      grpc_channel_credentials_release(c_channel_credentials)
    arguments_processor.un_c()
    self.references.append(target)
    self.references.append(arguments)

  def create_call(self, Call parent, int flags,
                  CompletionQueue queue not None,
                  method, host, object deadline):
    if queue.is_shutting_down:
      raise ValueError("queue must not be shutting down or shutdown")
    cdef grpc_slice method_slice = _slice_from_bytes(method)
    cdef grpc_slice host_slice
    cdef grpc_slice *host_slice_ptr = NULL
    if host is not None:
      host_slice = _slice_from_bytes(host)
      host_slice_ptr = &host_slice
    cdef Call operation_call = Call()
    operation_call.references = [self, queue]
    cdef grpc_call *parent_call = NULL
    if parent is not None:
      parent_call = parent.c_call
    operation_call.c_call = grpc_channel_create_call(
        self.c_channel, parent_call, flags,
        queue.c_completion_queue, method_slice, host_slice_ptr,
        _timespec_from_time(deadline), NULL)
    grpc_slice_unref(method_slice)
    if host_slice_ptr:
      grpc_slice_unref(host_slice)
    return operation_call

  def check_connectivity_state(self, bint try_to_connect):
    cdef grpc_connectivity_state result
    with nogil:
      result = grpc_channel_check_connectivity_state(self.c_channel,
                                                     try_to_connect)
    return result

  def watch_connectivity_state(
      self, grpc_connectivity_state last_observed_state,
      object deadline, CompletionQueue queue not None, tag):
    cdef _ConnectivityTag connectivity_tag = _ConnectivityTag(tag)
    cpython.Py_INCREF(connectivity_tag)
    grpc_channel_watch_connectivity_state(
        self.c_channel, last_observed_state, _timespec_from_time(deadline),
        queue.c_completion_queue, <cpython.PyObject *>connectivity_tag)

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
      grpc_channel_destroy(self.c_channel)
    grpc_shutdown()
