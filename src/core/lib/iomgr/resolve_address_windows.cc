/*
 *
 * Copyright 2015 gRPC authors.
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
#ifdef GRPC_WINSOCK_SOCKET

#include "src/core/lib/iomgr/sockaddr.h"

#include "src/core/lib/iomgr/resolve_address.h"

#include <inttypes.h>
#include <string.h>
#include <sys/types.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/log_windows.h>
#include <grpc/support/string_util.h>
#include <grpc/support/time.h>

#include "src/core/lib/gpr/string.h"
#include "src/core/lib/gprpp/host_port.h"
#include "src/core/lib/gprpp/thd.h"
#include "src/core/lib/iomgr/block_annotate.h"
#include "src/core/lib/iomgr/executor.h"
#include "src/core/lib/iomgr/iomgr_internal.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"

struct request {
  char* name;
  char* default_port;
  grpc_closure request_closure;
  grpc_closure* on_done;
  grpc_resolved_addresses** addresses;
};
static grpc_error* windows_blocking_resolve_address(
    const char* name, const char* default_port,
    grpc_resolved_addresses** addresses) {
  grpc_core::ExecCtx exec_ctx;
  struct addrinfo hints;
  struct addrinfo *result = NULL, *resp;
  int s;
  size_t i;
  grpc_error* error = GRPC_ERROR_NONE;

  /* parse name, splitting it into host and port parts */
  std::string host;
  std::string port;
  grpc_core::SplitHostPort(name, &host, &port);
  if (host.empty()) {
    char* msg;
    gpr_asprintf(&msg, "unparseable host:port: '%s'", name);
    error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(msg);
    gpr_free(msg);
    goto done;
  }
  if (port.empty()) {
    if (default_port == NULL) {
      char* msg;
      gpr_asprintf(&msg, "no port in name '%s'", name);
      error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(msg);
      gpr_free(msg);
      goto done;
    }
    port = default_port;
  }

  /* Call getaddrinfo */
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;     /* ipv4 or ipv6 */
  hints.ai_socktype = SOCK_STREAM; /* stream socket */
  hints.ai_flags = AI_PASSIVE;     /* for wildcard IP address */

  GRPC_SCHEDULING_START_BLOCKING_REGION;
  s = getaddrinfo(host.c_str(), port.c_str(), &hints, &result);
  GRPC_SCHEDULING_END_BLOCKING_REGION;
  if (s != 0) {
    error = GRPC_WSA_ERROR(WSAGetLastError(), "getaddrinfo");
    goto done;
  }

  /* Success path: set addrs non-NULL, fill it in */
  (*addresses) =
      (grpc_resolved_addresses*)gpr_malloc(sizeof(grpc_resolved_addresses));
  (*addresses)->naddrs = 0;
  for (resp = result; resp != NULL; resp = resp->ai_next) {
    (*addresses)->naddrs++;
  }
  (*addresses)->addrs = (grpc_resolved_address*)gpr_malloc(
      sizeof(grpc_resolved_address) * (*addresses)->naddrs);
  i = 0;
  for (resp = result; resp != NULL; resp = resp->ai_next) {
    memcpy(&(*addresses)->addrs[i].addr, resp->ai_addr, resp->ai_addrlen);
    (*addresses)->addrs[i].len = resp->ai_addrlen;
    i++;
  }

done:
  if (result) {
    freeaddrinfo(result);
  }
  return error;
}

/* Callback to be passed to grpc_executor to asynch-ify
 * grpc_blocking_resolve_address */
static void do_request_thread(void* rp, grpc_error* error) {
  request* r = (request*)rp;
  if (error == GRPC_ERROR_NONE) {
    error =
        grpc_blocking_resolve_address(r->name, r->default_port, r->addresses);
  } else {
    GRPC_ERROR_REF(error);
  }
  grpc_core::ExecCtx::Run(DEBUG_LOCATION, r->on_done, error);
  gpr_free(r->name);
  gpr_free(r->default_port);
  gpr_free(r);
}

static void windows_resolve_address(const char* name, const char* default_port,
                                    grpc_pollset_set* interested_parties,
                                    grpc_closure* on_done,
                                    grpc_resolved_addresses** addresses) {
  request* r = (request*)gpr_malloc(sizeof(request));
  GRPC_CLOSURE_INIT(&r->request_closure, do_request_thread, r, nullptr);
  r->name = gpr_strdup(name);
  r->default_port = gpr_strdup(default_port);
  r->on_done = on_done;
  r->addresses = addresses;
  grpc_core::Executor::Run(&r->request_closure, GRPC_ERROR_NONE,
                           grpc_core::ExecutorType::RESOLVER);
}

grpc_address_resolver_vtable grpc_windows_resolver_vtable = {
    windows_resolve_address, windows_blocking_resolve_address};
#endif
