/*
 *
 * Copyright 2015, Google Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 * copyright notice, this list of conditions and the following disclaimer
 * in the documentation and/or other materials provided with the
 * distribution.
 *     * Neither the name of Google Inc. nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include "src/core/lib/iomgr/port.h"
#ifdef GRPC_WINSOCK_SOCKET

#include "src/core/lib/iomgr/sockaddr.h"

#include "src/core/lib/iomgr/resolve_address.h"

#include <string.h>
#include <sys/types.h>

#include <grpc/support/alloc.h>
#include <grpc/support/host_port.h>
#include <grpc/support/log.h>
#include <grpc/support/log_windows.h>
#include <grpc/support/string_util.h>
#include <grpc/support/thd.h>
#include <grpc/support/time.h>
#include "src/core/lib/iomgr/executor.h"
#include "src/core/lib/iomgr/iomgr_internal.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"
#include "src/core/lib/support/block_annotate.h"
#include "src/core/lib/support/string.h"

typedef struct {
  char *name;
  char *default_port;
  grpc_closure request_closure;
  grpc_closure *on_done;
  grpc_resolved_addresses **addresses;
} request;

static grpc_error *blocking_resolve_address_impl(
    const char *name, const char *default_port,
    grpc_resolved_addresses **addresses) {
  struct addrinfo hints;
  struct addrinfo *result = NULL, *resp;
  char *host;
  char *port;
  int s;
  size_t i;
  grpc_error *error = GRPC_ERROR_NONE;

  /* parse name, splitting it into host and port parts */
  gpr_split_host_port(name, &host, &port);
  if (host == NULL) {
    char *msg;
    gpr_asprintf(&msg, "unparseable host:port: '%s'", name);
    error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(msg);
    gpr_free(msg);
    goto done;
  }
  if (port == NULL) {
    if (default_port == NULL) {
      char *msg;
      gpr_asprintf(&msg, "no port in name '%s'", name);
      error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(msg);
      gpr_free(msg);
      goto done;
    }
    port = gpr_strdup(default_port);
  }

  /* Call getaddrinfo */
  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;     /* ipv4 or ipv6 */
  hints.ai_socktype = SOCK_STREAM; /* stream socket */
  hints.ai_flags = AI_PASSIVE;     /* for wildcard IP address */

  GRPC_SCHEDULING_START_BLOCKING_REGION;
  s = getaddrinfo(host, port, &hints, &result);
  GRPC_SCHEDULING_END_BLOCKING_REGION;
  if (s != 0) {
    error = GRPC_WSA_ERROR(WSAGetLastError(), "getaddrinfo");
    goto done;
  }

  /* Success path: set addrs non-NULL, fill it in */
  (*addresses) = gpr_malloc(sizeof(grpc_resolved_addresses));
  (*addresses)->naddrs = 0;
  for (resp = result; resp != NULL; resp = resp->ai_next) {
    (*addresses)->naddrs++;
  }
  (*addresses)->addrs =
      gpr_malloc(sizeof(grpc_resolved_address) * (*addresses)->naddrs);
  i = 0;
  for (resp = result; resp != NULL; resp = resp->ai_next) {
    memcpy(&(*addresses)->addrs[i].addr, resp->ai_addr, resp->ai_addrlen);
    (*addresses)->addrs[i].len = resp->ai_addrlen;
    i++;
  }

  {
    for (i = 0; i < (*addresses)->naddrs; i++) {
      char *buf;
      grpc_sockaddr_to_string(&buf, &(*addresses)->addrs[i], 0);
      gpr_free(buf);
    }
  }

done:
  gpr_free(host);
  gpr_free(port);
  if (result) {
    freeaddrinfo(result);
  }
  return error;
}

grpc_error *(*grpc_blocking_resolve_address)(
    const char *name, const char *default_port,
    grpc_resolved_addresses **addresses) = blocking_resolve_address_impl;

/* Callback to be passed to grpc_executor to asynch-ify
 * grpc_blocking_resolve_address */
static void do_request_thread(grpc_exec_ctx *exec_ctx, void *rp,
                              grpc_error *error) {
  request *r = rp;
  if (error == GRPC_ERROR_NONE) {
    error =
        grpc_blocking_resolve_address(r->name, r->default_port, r->addresses);
  } else {
    GRPC_ERROR_REF(error);
  }
  grpc_closure_sched(exec_ctx, r->on_done, error);
  gpr_free(r->name);
  gpr_free(r->default_port);
  gpr_free(r);
}

void grpc_resolved_addresses_destroy(grpc_resolved_addresses *addrs) {
  if (addrs != NULL) {
    gpr_free(addrs->addrs);
  }
  gpr_free(addrs);
}

static void resolve_address_impl(grpc_exec_ctx *exec_ctx, const char *name,
                                 const char *default_port,
                                 grpc_pollset_set *interested_parties,
                                 grpc_closure *on_done,
                                 grpc_resolved_addresses **addresses) {
  request *r = gpr_malloc(sizeof(request));
  grpc_closure_init(&r->request_closure, do_request_thread, r,
                    grpc_executor_scheduler);
  r->name = gpr_strdup(name);
  r->default_port = gpr_strdup(default_port);
  r->on_done = on_done;
  r->addresses = addresses;
  grpc_closure_sched(exec_ctx, &r->request_closure, GRPC_ERROR_NONE);
}

void (*grpc_resolve_address)(
    grpc_exec_ctx *exec_ctx, const char *name, const char *default_port,
    grpc_pollset_set *interested_parties, grpc_closure *on_done,
    grpc_resolved_addresses **addresses) = resolve_address_impl;

#endif
