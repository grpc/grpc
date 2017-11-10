/*
 *
 * Copyright 2016 gRPC authors.
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

#include "src/core/lib/iomgr/port.h"

#ifdef GRPC_UV

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>

#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/iomgr_uv.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"
#include "src/core/lib/iomgr/tcp_client.h"
#include "src/core/lib/iomgr/tcp_uv.h"
#include "src/core/lib/iomgr/timer.h"

extern grpc_core::TraceFlag grpc_tcp_trace;

typedef struct grpc_uv_tcp_connect {
  uv_connect_t connect_req;
  grpc_timer alarm;
  grpc_closure on_alarm;
  uv_tcp_t* tcp_handle;
  grpc_closure* closure;
  grpc_endpoint** endpoint;
  int refs;
  char* addr_name;
  grpc_resource_quota* resource_quota;
} grpc_uv_tcp_connect;

static void uv_tcp_connect_cleanup(grpc_exec_ctx* exec_ctx,
                                   grpc_uv_tcp_connect* connect) {
  grpc_resource_quota_unref_internal(exec_ctx, connect->resource_quota);
  gpr_free(connect->addr_name);
  gpr_free(connect);
}

static void tcp_close_callback(uv_handle_t* handle) { gpr_free(handle); }

static void uv_tc_on_alarm(grpc_exec_ctx* exec_ctx, void* acp,
                           grpc_error* error) {
  int done;
  grpc_uv_tcp_connect* connect = (grpc_uv_tcp_connect*)acp;
  if (grpc_tcp_trace.enabled()) {
    const char* str = grpc_error_string(error);
    gpr_log(GPR_DEBUG, "CLIENT_CONNECT: %s: on_alarm: error=%s",
            connect->addr_name, str);
  }
  if (error == GRPC_ERROR_NONE) {
    /* error == NONE implies that the timer ran out, and wasn't cancelled. If
       it was cancelled, then the handler that cancelled it also should close
       the handle, if applicable */
    uv_close((uv_handle_t*)connect->tcp_handle, tcp_close_callback);
  }
  done = (--connect->refs == 0);
  if (done) {
    uv_tcp_connect_cleanup(exec_ctx, connect);
  }
}

static void uv_tc_on_connect(uv_connect_t* req, int status) {
  grpc_uv_tcp_connect* connect = (grpc_uv_tcp_connect*)req->data;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
  grpc_error* error = GRPC_ERROR_NONE;
  int done;
  grpc_closure* closure = connect->closure;
  grpc_timer_cancel(&exec_ctx, &connect->alarm);
  if (status == 0) {
    *connect->endpoint = grpc_tcp_create(
        connect->tcp_handle, connect->resource_quota, connect->addr_name);
  } else {
    error = GRPC_ERROR_CREATE_FROM_STATIC_STRING(
        "Failed to connect to remote host");
    error = grpc_error_set_int(error, GRPC_ERROR_INT_ERRNO, -status);
    error =
        grpc_error_set_str(error, GRPC_ERROR_STR_OS_ERROR,
                           grpc_slice_from_static_string(uv_strerror(status)));
    if (status == UV_ECANCELED) {
      error =
          grpc_error_set_str(error, GRPC_ERROR_STR_OS_ERROR,
                             grpc_slice_from_static_string("Timeout occurred"));
      // This should only happen if the handle is already closed
    } else {
      error = grpc_error_set_str(
          error, GRPC_ERROR_STR_OS_ERROR,
          grpc_slice_from_static_string(uv_strerror(status)));
      uv_close((uv_handle_t*)connect->tcp_handle, tcp_close_callback);
    }
  }
  done = (--connect->refs == 0);
  if (done) {
    grpc_exec_ctx_flush(&exec_ctx);
    uv_tcp_connect_cleanup(&exec_ctx, connect);
  }
  GRPC_CLOSURE_SCHED(&exec_ctx, closure, error);
  grpc_exec_ctx_finish(&exec_ctx);
}

static void tcp_client_connect_impl(grpc_exec_ctx* exec_ctx,
                                    grpc_closure* closure, grpc_endpoint** ep,
                                    grpc_pollset_set* interested_parties,
                                    const grpc_channel_args* channel_args,
                                    const grpc_resolved_address* resolved_addr,
                                    grpc_millis deadline) {
  grpc_uv_tcp_connect* connect;
  grpc_resource_quota* resource_quota = grpc_resource_quota_create(NULL);
  (void)channel_args;
  (void)interested_parties;

  GRPC_UV_ASSERT_SAME_THREAD();

  if (channel_args != NULL) {
    for (size_t i = 0; i < channel_args->num_args; i++) {
      if (0 == strcmp(channel_args->args[i].key, GRPC_ARG_RESOURCE_QUOTA)) {
        grpc_resource_quota_unref_internal(exec_ctx, resource_quota);
        resource_quota = grpc_resource_quota_ref_internal(
            (grpc_resource_quota*)channel_args->args[i].value.pointer.p);
      }
    }
  }

  connect = (grpc_uv_tcp_connect*)gpr_zalloc(sizeof(grpc_uv_tcp_connect));
  connect->closure = closure;
  connect->endpoint = ep;
  connect->tcp_handle = (uv_tcp_t*)gpr_malloc(sizeof(uv_tcp_t));
  connect->addr_name = grpc_sockaddr_to_uri(resolved_addr);
  connect->resource_quota = resource_quota;
  uv_tcp_init(uv_default_loop(), connect->tcp_handle);
  connect->connect_req.data = connect;
  connect->refs = 2;  // One for the connect operation, one for the timer.

  if (grpc_tcp_trace.enabled()) {
    gpr_log(GPR_DEBUG, "CLIENT_CONNECT: %s: asynchronously connecting",
            connect->addr_name);
  }

  // TODO(murgatroid99): figure out what the return value here means
  uv_tcp_connect(&connect->connect_req, connect->tcp_handle,
                 (const struct sockaddr*)resolved_addr->addr, uv_tc_on_connect);
  GRPC_CLOSURE_INIT(&connect->on_alarm, uv_tc_on_alarm, connect,
                    grpc_schedule_on_exec_ctx);
  grpc_timer_init(exec_ctx, &connect->alarm, deadline, &connect->on_alarm);
}

// overridden by api_fuzzer.c
extern "C" {
void (*grpc_tcp_client_connect_impl)(
    grpc_exec_ctx* exec_ctx, grpc_closure* closure, grpc_endpoint** ep,
    grpc_pollset_set* interested_parties, const grpc_channel_args* channel_args,
    const grpc_resolved_address* addr,
    grpc_millis deadline) = tcp_client_connect_impl;
}

void grpc_tcp_client_connect(grpc_exec_ctx* exec_ctx, grpc_closure* closure,
                             grpc_endpoint** ep,
                             grpc_pollset_set* interested_parties,
                             const grpc_channel_args* channel_args,
                             const grpc_resolved_address* addr,
                             grpc_millis deadline) {
  grpc_tcp_client_connect_impl(exec_ctx, closure, ep, interested_parties,
                               channel_args, addr, deadline);
}

#endif /* GRPC_UV */
