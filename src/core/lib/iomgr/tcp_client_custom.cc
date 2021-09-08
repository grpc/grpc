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

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/address_utils/sockaddr_utils.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/iomgr_custom.h"
#include "src/core/lib/iomgr/port.h"
#include "src/core/lib/iomgr/tcp_client.h"
#include "src/core/lib/iomgr/tcp_custom.h"
#include "src/core/lib/iomgr/timer.h"

extern grpc_core::TraceFlag grpc_tcp_trace;
extern grpc_socket_vtable* grpc_custom_socket_vtable;

struct grpc_custom_tcp_connect {
  grpc_custom_socket* socket;
  grpc_timer alarm;
  grpc_closure on_alarm;
  grpc_closure* closure;
  grpc_endpoint** endpoint;
  int refs;
  std::string addr_name;
  grpc_slice_allocator* slice_allocator;
};

static void custom_tcp_connect_cleanup(grpc_custom_tcp_connect* connect) {
  if (connect->slice_allocator != nullptr) {
    grpc_slice_allocator_destroy(connect->slice_allocator);
  }
  grpc_custom_socket* socket = connect->socket;
  delete connect;
  socket->refs--;
  if (socket->refs == 0) {
    grpc_custom_socket_vtable->destroy(socket);
    gpr_free(socket);
  }
}

static void custom_close_callback(grpc_custom_socket* /*socket*/) {}

static void on_alarm(void* acp, grpc_error_handle error) {
  int done;
  grpc_custom_socket* socket = static_cast<grpc_custom_socket*>(acp);
  grpc_custom_tcp_connect* connect = socket->connector;
  if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
    gpr_log(GPR_INFO, "CLIENT_CONNECT: %s: on_alarm: error=%s",
            connect->addr_name.c_str(), grpc_error_std_string(error).c_str());
  }
  if (error == GRPC_ERROR_NONE) {
    /* error == NONE implies that the timer ran out, and wasn't cancelled. If
       it was cancelled, then the handler that cancelled it also should close
       the handle, if applicable */
    grpc_custom_socket_vtable->close(socket, custom_close_callback);
  }
  done = (--connect->refs == 0);
  if (done) {
    custom_tcp_connect_cleanup(connect);
  }
}

static void custom_connect_callback_internal(grpc_custom_socket* socket,
                                             grpc_error_handle error) {
  grpc_custom_tcp_connect* connect = socket->connector;
  int done;
  grpc_closure* closure = connect->closure;
  grpc_timer_cancel(&connect->alarm);
  if (error == GRPC_ERROR_NONE) {
    *connect->endpoint = custom_tcp_endpoint_create(
        socket, connect->slice_allocator, connect->addr_name.c_str());
    connect->slice_allocator = nullptr;
  }
  done = (--connect->refs == 0);
  if (done) {
    grpc_core::ExecCtx::Get()->Flush();
    custom_tcp_connect_cleanup(connect);
  }
  grpc_core::ExecCtx::Run(DEBUG_LOCATION, closure, error);
}

static void custom_connect_callback(grpc_custom_socket* socket,
                                    grpc_error_handle error) {
  grpc_core::ApplicationCallbackExecCtx callback_exec_ctx;
  if (grpc_core::ExecCtx::Get() == nullptr) {
    /* If we are being run on a thread which does not have an exec_ctx created
     * yet, we should create one. */
    grpc_core::ExecCtx exec_ctx;
    custom_connect_callback_internal(socket, error);
  } else {
    custom_connect_callback_internal(socket, error);
  }
}

static void tcp_connect(grpc_closure* closure, grpc_endpoint** ep,
                        grpc_slice_allocator* slice_allocator,
                        grpc_pollset_set* interested_parties,
                        const grpc_channel_args* channel_args,
                        const grpc_resolved_address* resolved_addr,
                        grpc_millis deadline) {
  GRPC_CUSTOM_IOMGR_ASSERT_SAME_THREAD();
  (void)channel_args;
  (void)interested_parties;
  grpc_custom_socket* socket =
      static_cast<grpc_custom_socket*>(gpr_malloc(sizeof(grpc_custom_socket)));
  socket->refs = 2;
  grpc_custom_socket_vtable->init(socket, GRPC_AF_UNSPEC);
  grpc_custom_tcp_connect* connect = new grpc_custom_tcp_connect();
  connect->closure = closure;
  connect->endpoint = ep;
  connect->addr_name = grpc_sockaddr_to_uri(resolved_addr);
  connect->slice_allocator = slice_allocator;
  connect->socket = socket;
  socket->connector = connect;
  socket->endpoint = nullptr;
  socket->listener = nullptr;
  connect->refs = 2;

  if (GRPC_TRACE_FLAG_ENABLED(grpc_tcp_trace)) {
    gpr_log(GPR_INFO, "CLIENT_CONNECT: %p %s: asynchronously connecting",
            socket, connect->addr_name.c_str());
  }

  GRPC_CLOSURE_INIT(&connect->on_alarm, on_alarm, socket,
                    grpc_schedule_on_exec_ctx);
  grpc_timer_init(&connect->alarm, deadline, &connect->on_alarm);
  grpc_custom_socket_vtable->connect(
      socket, reinterpret_cast<const grpc_sockaddr*>(resolved_addr->addr),
      resolved_addr->len, custom_connect_callback);
}

grpc_tcp_client_vtable custom_tcp_client_vtable = {tcp_connect};
