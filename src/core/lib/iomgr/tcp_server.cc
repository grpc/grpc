/*
 *
 * Copyright 2018 gRPC authors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <grpc/support/port_platform.h>

#include "src/core/lib/iomgr/tcp_server.h"

grpc_tcp_server_vtable* grpc_tcp_server_impl;

grpc_error_handle grpc_tcp_server_create(grpc_closure* shutdown_complete,
                                         const grpc_channel_args* args,
                                         grpc_tcp_server** server) {
  return grpc_tcp_server_impl->create(shutdown_complete, args, server);
}

void grpc_tcp_server_start(grpc_tcp_server* server,
                           const std::vector<grpc_pollset*>* pollsets,
                           grpc_tcp_server_cb on_accept_cb, void* cb_arg) {
  grpc_tcp_server_impl->start(server, pollsets, on_accept_cb, cb_arg);
}

grpc_error_handle grpc_tcp_server_add_port(grpc_tcp_server* s,
                                           const grpc_resolved_address* addr,
                                           int* out_port) {
  return grpc_tcp_server_impl->add_port(s, addr, out_port);
}

grpc_core::TcpServerFdHandler* grpc_tcp_server_create_fd_handler(
    grpc_tcp_server* s) {
  return grpc_tcp_server_impl->create_fd_handler(s);
}

unsigned grpc_tcp_server_port_fd_count(grpc_tcp_server* s,
                                       unsigned port_index) {
  return grpc_tcp_server_impl->port_fd_count(s, port_index);
}

int grpc_tcp_server_port_fd(grpc_tcp_server* s, unsigned port_index,
                            unsigned fd_index) {
  return grpc_tcp_server_impl->port_fd(s, port_index, fd_index);
}

grpc_tcp_server* grpc_tcp_server_ref(grpc_tcp_server* s) {
  return grpc_tcp_server_impl->ref(s);
}

void grpc_tcp_server_shutdown_starting_add(grpc_tcp_server* s,
                                           grpc_closure* shutdown_starting) {
  grpc_tcp_server_impl->shutdown_starting_add(s, shutdown_starting);
}

void grpc_tcp_server_unref(grpc_tcp_server* s) {
  grpc_tcp_server_impl->unref(s);
}

void grpc_tcp_server_shutdown_listeners(grpc_tcp_server* s) {
  grpc_tcp_server_impl->shutdown_listeners(s);
}

void grpc_set_tcp_server_impl(grpc_tcp_server_vtable* impl) {
  grpc_tcp_server_impl = impl;
}
