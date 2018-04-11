# Copyright 2017 gRPC authors.
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

from libc.stdint cimport uint32_t

cdef extern from "grpc/impl/codegen/slice.h":
  struct grpc_slice_buffer:
    int count

cdef extern from "src/core/lib/iomgr/error.h":
  struct grpc_error:
    pass

cdef extern from "src/core/lib/iomgr/gevent_util.h":
  grpc_error* grpc_socket_error(char* error) 
  char* grpc_slice_buffer_start(grpc_slice_buffer* buffer, int i)
  int grpc_slice_buffer_length(grpc_slice_buffer* buffer, int i)

cdef extern from "src/core/lib/iomgr/sockaddr.h":
  ctypedef struct grpc_sockaddr:
    pass

cdef extern from "src/core/lib/iomgr/resolve_address.h":
  ctypedef struct grpc_resolved_addresses:
    size_t naddrs
    grpc_resolved_address* addrs

  ctypedef struct grpc_resolved_address:
    char[128] addr
    size_t len

cdef extern from "src/core/lib/iomgr/resolve_address_custom.h":
  struct grpc_custom_resolver:
    pass

  struct grpc_custom_resolver_vtable:
    grpc_error* (*resolve)(char* host, char* port, grpc_resolved_addresses** res);
    void (*resolve_async)(grpc_custom_resolver* resolver, char* host, char* port);

  void grpc_custom_resolve_callback(grpc_custom_resolver* resolver,
                                    grpc_resolved_addresses* result,
                                    grpc_error* error);

cdef extern from "src/core/lib/iomgr/tcp_custom.h":
  struct grpc_custom_socket:
    void* impl
    # We don't care about the rest of the fields
  ctypedef void (*grpc_custom_connect_callback)(grpc_custom_socket* socket,
                                             grpc_error* error)
  ctypedef void (*grpc_custom_write_callback)(grpc_custom_socket* socket,
                                           grpc_error* error)
  ctypedef void (*grpc_custom_read_callback)(grpc_custom_socket* socket,
                                          size_t nread, grpc_error* error)
  ctypedef void (*grpc_custom_accept_callback)(grpc_custom_socket* socket,
                                            grpc_custom_socket* client,
                                            grpc_error* error)
  ctypedef void (*grpc_custom_close_callback)(grpc_custom_socket* socket)

  struct grpc_socket_vtable:
      grpc_error* (*init)(grpc_custom_socket* socket, int domain);
      void (*connect)(grpc_custom_socket* socket, const grpc_sockaddr* addr,
                      size_t len, grpc_custom_connect_callback cb);
      void (*destroy)(grpc_custom_socket* socket);
      void (*shutdown)(grpc_custom_socket* socket);
      void (*close)(grpc_custom_socket* socket, grpc_custom_close_callback cb);
      void (*write)(grpc_custom_socket* socket, grpc_slice_buffer* slices,
                    grpc_custom_write_callback cb);
      void (*read)(grpc_custom_socket* socket, char* buffer, size_t length,
                   grpc_custom_read_callback cb);
      grpc_error* (*getpeername)(grpc_custom_socket* socket,
                                 const grpc_sockaddr* addr, int* len);
      grpc_error* (*getsockname)(grpc_custom_socket* socket,
                             const grpc_sockaddr* addr, int* len);
      grpc_error* (*bind)(grpc_custom_socket* socket, const grpc_sockaddr* addr,
                          size_t len, int flags);
      grpc_error* (*listen)(grpc_custom_socket* socket);
      void (*accept)(grpc_custom_socket* socket, grpc_custom_socket* client,
                     grpc_custom_accept_callback cb);

cdef extern from "src/core/lib/iomgr/timer_custom.h":
  struct grpc_custom_timer:
    void* timer
    int timeout_ms
     # We don't care about the rest of the fields

  struct grpc_custom_timer_vtable:
    void (*start)(grpc_custom_timer* t);
    void (*stop)(grpc_custom_timer* t);

  void grpc_custom_timer_callback(grpc_custom_timer* t, grpc_error* error);

cdef extern from "src/core/lib/iomgr/pollset_custom.h":
  struct grpc_custom_poller_vtable:
    void (*init)()
    void (*poll)(size_t timeout_ms)
    void (*kick)()
    void (*shutdown)()

cdef extern from "src/core/lib/iomgr/iomgr_custom.h":
  void grpc_custom_iomgr_init(grpc_socket_vtable* socket,
                            grpc_custom_resolver_vtable* resolver,
                            grpc_custom_timer_vtable* timer,
                            grpc_custom_poller_vtable* poller);

cdef extern from "src/core/lib/iomgr/sockaddr_utils.h":
  int grpc_sockaddr_get_port(const grpc_resolved_address *addr);
  int grpc_sockaddr_to_string(char **out, const grpc_resolved_address *addr,
                              int normalize);
  void grpc_string_to_sockaddr(grpc_resolved_address *out, char* addr, int port);
  int grpc_sockaddr_set_port(const grpc_resolved_address *resolved_addr,
                             int port)
  const char* grpc_sockaddr_get_uri_scheme(const grpc_resolved_address* resolved_addr)


cdef class TimerWrapper:

  cdef grpc_custom_timer *c_timer
  cdef object timer
  cdef object event

cdef class SocketWrapper:
  cdef object sockopts
  cdef object socket
  cdef object closed
  cdef grpc_custom_socket *c_socket
  cdef char* c_buffer
  cdef size_t len
  cdef grpc_custom_socket *accepting_socket

  cdef grpc_custom_connect_callback connect_cb
  cdef grpc_custom_write_callback write_cb
  cdef grpc_custom_read_callback read_cb
  cdef grpc_custom_accept_callback accept_cb
  cdef grpc_custom_close_callback close_cb


cdef class ResolveWrapper:
  cdef grpc_custom_resolver *c_resolver
  cdef char* c_host
  cdef char* c_port
