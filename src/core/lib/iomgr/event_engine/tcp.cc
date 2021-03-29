/*
 *
 * Copyright 2021 gRPC authors.
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

#if defined(GRPC_EVENT_ENGINE_TEST)

#include <grpc/support/port_platform.h>

#include <grpc/event_engine/event_engine.h>

#include "src/core/lib/iomgr/event_engine/util.h"
#include "src/core/lib/iomgr/tcp_client.h"
#include "src/core/lib/iomgr/tcp_server.h"

namespace {
using ::grpc_io::experimental::EventEngine;
}  // namespace

static void tcp_connect(grpc_closure* on_connect, grpc_endpoint** endpoint,
                        grpc_pollset_set* interested_parties,
                        const grpc_channel_args* channel_args,
                        const grpc_resolved_address* addr,
                        grpc_millis deadline) {
  EventEngine::Endpoint::OnConnectCallback ee_on_connect =
      grpc_io::experimental::event_engine_closure_to_on_connect_callback(
          on_connect);
  // Needs:
  //  * grpc_endpoint -> EventEngine::Endpoint
  //  * Ignore interested_parties? Hope so.
  //  * convert channel_args to ChannelArgs
  //  * grpc_resolved_address -> ResolvedAddress
  //  * retrieve EventEngine from Endpoint or from channel_args

  (void)interested_parties;
  (void)channel_args;
  (void)addr;
  (void)deadline;
}

static grpc_error* tcp_server_create(grpc_closure* shutdown_complete,
                                     const grpc_channel_args* args,
                                     grpc_tcp_server** server) {
  (void)shutdown_complete;
  (void)args;
  (void)server;
  return GRPC_ERROR_NONE;
}

static void tcp_server_start(grpc_tcp_server* server,
                             const std::vector<grpc_pollset*>* pollsets,
                             grpc_tcp_server_cb on_accept_cb, void* cb_arg) {
  (void)server;
  (void)pollsets;
  (void)on_accept_cb;
  (void)cb_arg;
}

static grpc_error* tcp_server_add_port(grpc_tcp_server* s,
                                       const grpc_resolved_address* addr,
                                       int* out_port) {
  (void)s;
  (void)addr;
  (void)out_port;
  return GRPC_ERROR_NONE;
}

static grpc_core::TcpServerFdHandler* tcp_server_create_fd_handler(
    grpc_tcp_server* s) {
  (void)s;
  // TODO(hork): verify
  return nullptr;
}

static unsigned tcp_server_port_fd_count(grpc_tcp_server* s,
                                         unsigned port_index) {
  (void)s;
  (void)port_index;
  return 0;
}

static int tcp_server_port_fd(grpc_tcp_server* s, unsigned port_index,
                              unsigned fd_index) {
  (void)s;
  (void)port_index;
  (void)fd_index;
  return -1;
}

static grpc_tcp_server* tcp_server_ref(grpc_tcp_server* s) {
  // TODO(hork): verify
  // gpr_ref_non_zero(&s->refs);
  (void)s;
  return s;
}
static void tcp_server_shutdown_starting_add(grpc_tcp_server* s,
                                             grpc_closure* shutdown_starting) {
  (void)s;
  (void)shutdown_starting;
}
static void tcp_server_unref(grpc_tcp_server* s) { (void)s; }
static void tcp_server_shutdown_listeners(grpc_tcp_server* s) { (void)s; }

grpc_tcp_client_vtable grpc_event_engine_tcp_client_vtable = {tcp_connect};
grpc_tcp_server_vtable grpc_event_engine_tcp_server_vtable = {
    tcp_server_create,        tcp_server_start,
    tcp_server_add_port,      tcp_server_create_fd_handler,
    tcp_server_port_fd_count, tcp_server_port_fd,
    tcp_server_ref,           tcp_server_shutdown_starting_add,
    tcp_server_unref,         tcp_server_shutdown_listeners};

#endif
