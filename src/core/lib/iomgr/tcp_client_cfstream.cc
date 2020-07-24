
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

#include "src/core/lib/iomgr/port.h"

#ifdef GRPC_CFSTREAM_CLIENT

#include <CoreFoundation/CoreFoundation.h>

#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/sync.h>

#include <netinet/in.h>

#include "src/core/lib/channel/channel_args.h"
#include "src/core/lib/gprpp/host_port.h"
#include "src/core/lib/iomgr/cfstream_handle.h"
#include "src/core/lib/iomgr/closure.h"
#include "src/core/lib/iomgr/endpoint_cfstream.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/error_cfstream.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"
#include "src/core/lib/iomgr/tcp_client.h"
#include "src/core/lib/iomgr/timer.h"

extern grpc_core::TraceFlag grpc_tcp_trace;

struct CFStreamConnect {
  gpr_mu mu;
  gpr_refcount refcount;

  CFReadStreamRef read_stream;
  CFWriteStreamRef write_stream;
  CFStreamHandle* stream_handle;

  grpc_timer alarm;
  grpc_closure on_alarm;
  grpc_closure on_open;

  bool read_stream_open;
  bool write_stream_open;
  bool failed;

  grpc_closure* closure;
  grpc_endpoint** endpoint;
  int refs;
  std::string addr_name;
  grpc_resource_quota* resource_quota;
};

static void CFStreamConnectCleanup(CFStreamConnect* connect) {
  grpc_resource_quota_unref_internal(connect->resource_quota);
  CFSTREAM_HANDLE_UNREF(connect->stream_handle, "async connect clean up");
  CFRelease(connect->read_stream);
  CFRelease(connect->write_stream);
  gpr_mu_destroy(&connect->mu);
  delete connect;
}

static void OnAlarm(void* arg, grpc_error* error) {
  CFStreamConnect* connect = static_cast<CFStreamConnect*>(arg);
  if (grpc_tcp_trace.enabled()) {
    gpr_log(GPR_DEBUG, "CLIENT_CONNECT :%p OnAlarm, error:%p", connect, error);
  }
  gpr_mu_lock(&connect->mu);
  grpc_closure* closure = connect->closure;
  connect->closure = nil;
  const bool done = (--connect->refs == 0);
  gpr_mu_unlock(&connect->mu);
  // Only schedule a callback once, by either OnAlarm or OnOpen. The
  // first one issues callback while the second one does cleanup.
  if (done) {
    CFStreamConnectCleanup(connect);
  } else {
    grpc_error* error =
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("connect() timed out");
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, closure, error);
  }
}

static void OnOpen(void* arg, grpc_error* error) {
  CFStreamConnect* connect = static_cast<CFStreamConnect*>(arg);
  if (grpc_tcp_trace.enabled()) {
    gpr_log(GPR_DEBUG, "CLIENT_CONNECT :%p OnOpen, error:%p", connect, error);
  }
  gpr_mu_lock(&connect->mu);
  grpc_timer_cancel(&connect->alarm);
  grpc_closure* closure = connect->closure;
  connect->closure = nil;

  bool done = (--connect->refs == 0);
  grpc_endpoint** endpoint = connect->endpoint;

  // Only schedule a callback once, by either OnAlarm or OnOpen. The
  // first one issues callback while the second one does cleanup.
  if (done) {
    gpr_mu_unlock(&connect->mu);
    CFStreamConnectCleanup(connect);
  } else {
    if (error == GRPC_ERROR_NONE) {
      CFErrorRef stream_error = CFReadStreamCopyError(connect->read_stream);
      if (stream_error == NULL) {
        stream_error = CFWriteStreamCopyError(connect->write_stream);
      }
      if (stream_error) {
        error = GRPC_ERROR_CREATE_FROM_CFERROR(stream_error, "connect() error");
        CFRelease(stream_error);
      }
      if (error == GRPC_ERROR_NONE) {
        *endpoint = grpc_cfstream_endpoint_create(
            connect->read_stream, connect->write_stream,
            connect->addr_name.c_str(), connect->resource_quota,
            connect->stream_handle);
      }
    } else {
      GRPC_ERROR_REF(error);
    }
    gpr_mu_unlock(&connect->mu);
    grpc_core::ExecCtx::Run(DEBUG_LOCATION, closure, error);
  }
}

static void ParseResolvedAddress(const grpc_resolved_address* addr,
                                 CFStringRef* host, int* port) {
  std::string host_port = grpc_sockaddr_to_string(addr, true);
  std::string host_string;
  std::string port_string;
  grpc_core::SplitHostPort(host_port, &host_string, &port_string);
  *host = CFStringCreateWithCString(NULL, host_string.c_str(),
                                    kCFStringEncodingUTF8);
  *port = grpc_sockaddr_get_port(addr);
}

static void CFStreamClientConnect(grpc_closure* closure, grpc_endpoint** ep,
                                  grpc_pollset_set* interested_parties,
                                  const grpc_channel_args* channel_args,
                                  const grpc_resolved_address* resolved_addr,
                                  grpc_millis deadline) {
  CFStreamConnect* connect = new CFStreamConnect();
  connect->closure = closure;
  connect->endpoint = ep;
  connect->addr_name = grpc_sockaddr_to_uri(resolved_addr);
  // connect->resource_quota = resource_quota;
  connect->refs = 2;  // One for the connect operation, one for the timer.
  gpr_ref_init(&connect->refcount, 1);
  gpr_mu_init(&connect->mu);

  if (grpc_tcp_trace.enabled()) {
    gpr_log(GPR_DEBUG, "CLIENT_CONNECT: %p, %s: asynchronously connecting",
            connect, connect->addr_name.c_str());
  }

  grpc_resource_quota* resource_quota = grpc_resource_quota_create(NULL);
  if (channel_args != NULL) {
    for (size_t i = 0; i < channel_args->num_args; i++) {
      if (0 == strcmp(channel_args->args[i].key, GRPC_ARG_RESOURCE_QUOTA)) {
        grpc_resource_quota_unref_internal(resource_quota);
        resource_quota = grpc_resource_quota_ref_internal(
            (grpc_resource_quota*)channel_args->args[i].value.pointer.p);
      }
    }
  }
  connect->resource_quota = resource_quota;

  CFReadStreamRef read_stream;
  CFWriteStreamRef write_stream;

  CFStringRef host;
  int port;
  ParseResolvedAddress(resolved_addr, &host, &port);
  CFStreamCreatePairWithSocketToHost(NULL, host, port, &read_stream,
                                     &write_stream);
  CFRelease(host);
  connect->read_stream = read_stream;
  connect->write_stream = write_stream;
  connect->stream_handle =
      CFStreamHandle::CreateStreamHandle(read_stream, write_stream);
  GRPC_CLOSURE_INIT(&connect->on_open, OnOpen, static_cast<void*>(connect),
                    grpc_schedule_on_exec_ctx);
  connect->stream_handle->NotifyOnOpen(&connect->on_open);
  GRPC_CLOSURE_INIT(&connect->on_alarm, OnAlarm, connect,
                    grpc_schedule_on_exec_ctx);
  gpr_mu_lock(&connect->mu);
  CFReadStreamOpen(read_stream);
  CFWriteStreamOpen(write_stream);
  grpc_timer_init(&connect->alarm, deadline, &connect->on_alarm);
  gpr_mu_unlock(&connect->mu);
}

grpc_tcp_client_vtable grpc_cfstream_client_vtable = {CFStreamClientConnect};

#endif /* GRPC_CFSTREAM_CLIENT */
