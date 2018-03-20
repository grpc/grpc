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

import logging
import time
import grpc

cdef grpc_ssl_certificate_config_reload_status _server_cert_config_fetcher_wrapper(
        void* user_data, grpc_ssl_server_certificate_config **config) with gil:
  # This is a credentials.ServerCertificateConfig
  cdef ServerCertificateConfig cert_config = None
  if not user_data:
    raise ValueError('internal error: user_data must be specified')
  credentials = <ServerCredentials>user_data
  if not credentials.initial_cert_config_fetched:
    # C-core is asking for the initial cert config
    credentials.initial_cert_config_fetched = True
    cert_config = credentials.initial_cert_config._certificate_configuration
  else:
    user_cb = credentials.cert_config_fetcher
    try:
      cert_config_wrapper = user_cb()
    except Exception:
      logging.exception('Error fetching certificate config')
      return GRPC_SSL_CERTIFICATE_CONFIG_RELOAD_FAIL
    if cert_config_wrapper is None:
      return GRPC_SSL_CERTIFICATE_CONFIG_RELOAD_UNCHANGED
    elif not isinstance(
        cert_config_wrapper, grpc.ServerCertificateConfiguration):
      logging.error(
          'Error fetching certificate configuration: certificate '
          'configuration must be of type grpc.ServerCertificateConfiguration, '
          'not %s' % type(cert_config_wrapper).__name__)
      return GRPC_SSL_CERTIFICATE_CONFIG_RELOAD_FAIL
    else:
      cert_config = cert_config_wrapper._certificate_configuration
  config[0] = <grpc_ssl_server_certificate_config*>cert_config.c_cert_config
  # our caller will assume ownership of memory, so we have to recreate
  # a copy of c_cert_config here
  cert_config.c_cert_config = grpc_ssl_server_certificate_config_create(
      cert_config.c_pem_root_certs, cert_config.c_ssl_pem_key_cert_pairs,
      cert_config.c_ssl_pem_key_cert_pairs_count)
  return GRPC_SSL_CERTIFICATE_CONFIG_RELOAD_NEW

cdef class Server:

  def __cinit__(self, object arguments):
    grpc_init()
    self.references = []
    self.registered_completion_queues = []
    self._vtable.copy = &_copy_pointer
    self._vtable.destroy = &_destroy_pointer
    self._vtable.cmp = &_compare_pointer
    cdef _ArgumentsProcessor arguments_processor = _ArgumentsProcessor(
        arguments)
    cdef grpc_channel_args *c_arguments = arguments_processor.c(&self._vtable)
    self.c_server = grpc_server_create(c_arguments, NULL)
    arguments_processor.un_c()
    self.references.append(arguments)
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
    cdef _RequestCallTag request_call_tag = _RequestCallTag(tag)
    request_call_tag.prepare()
    cpython.Py_INCREF(request_call_tag)
    return grpc_server_request_call(
        self.c_server, &request_call_tag.call.c_call,
        &request_call_tag.call_details.c_details,
        &request_call_tag.c_invocation_metadata,
        call_queue.c_completion_queue, server_queue.c_completion_queue,
        <cpython.PyObject *>request_call_tag)

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
    self.backup_shutdown_queue.poll(deadline=time.time())

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
    cdef _ServerShutdownTag server_shutdown_tag = _ServerShutdownTag(tag, self)
    cpython.Py_INCREF(server_shutdown_tag)
    with nogil:
      grpc_server_shutdown_and_notify(
          self.c_server, queue.c_completion_queue,
          <cpython.PyObject *>server_shutdown_tag)

  def shutdown(self, CompletionQueue queue not None, tag):
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
    # called only after our server shutdown tag has emerged from a completion
    # queue.
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
