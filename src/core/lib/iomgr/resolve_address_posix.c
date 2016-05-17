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

#include <grpc/support/port_platform.h>
#ifdef GPR_POSIX_SOCKET

#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/sockaddr.h"

#include <string.h>
#include <sys/types.h>

#include <grpc/support/alloc.h>
#include <grpc/support/host_port.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/thd.h>
#include <grpc/support/time.h>
#include <grpc/support/useful.h>
#include "src/core/lib/iomgr/executor.h"
#include "src/core/lib/iomgr/iomgr_internal.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"
#include "src/core/lib/iomgr/unix_sockets_posix.h"
#include "src/core/lib/support/block_annotate.h"
#include "src/core/lib/support/string.h"

typedef struct {
  char *name;
  char *default_port;
  grpc_resolve_cb cb;
  grpc_closure request_closure;
  void *arg;
} request;

static grpc_resolved_addresses *blocking_resolve_address_impl(
    const char *name, const char *default_port) {
  struct addrinfo hints;
  struct addrinfo *result = NULL, *resp;
  char *host;
  char *port;
  int s;
  size_t i;
  grpc_resolved_addresses *addrs = NULL;

  if (name[0] == 'u' && name[1] == 'n' && name[2] == 'i' && name[3] == 'x' &&
      name[4] == ':' && name[5] != 0) {
    return grpc_resolve_unix_domain_address(name + 5);
  }

  /* parse name, splitting it into host and port parts */
  gpr_split_host_port(name, &host, &port);
  if (host == NULL) {
    gpr_log(GPR_ERROR, "unparseable host:port: '%s'", name);
    goto done;
  }
  if (port == NULL) {
    if (default_port == NULL) {
      gpr_log(GPR_ERROR, "no port in name '%s'", name);
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
    /* Retry if well-known service name is recognized */
    char *svc[][2] = {{"http", "80"}, {"https", "443"}};
    for (i = 0; i < GPR_ARRAY_SIZE(svc); i++) {
      if (strcmp(port, svc[i][0]) == 0) {
        GRPC_SCHEDULING_START_BLOCKING_REGION;
        s = getaddrinfo(host, svc[i][1], &hints, &result);
        GRPC_SCHEDULING_END_BLOCKING_REGION;
        break;
      }
    }
  }

  if (s != 0) {
    gpr_log(GPR_ERROR, "getaddrinfo: %s", gai_strerror(s));
    goto done;
  }

  /* Success path: set addrs non-NULL, fill it in */
  addrs = gpr_malloc(sizeof(grpc_resolved_addresses));
  addrs->naddrs = 0;
  for (resp = result; resp != NULL; resp = resp->ai_next) {
    addrs->naddrs++;
  }
  addrs->addrs = gpr_malloc(sizeof(grpc_resolved_address) * addrs->naddrs);
  i = 0;
  for (resp = result; resp != NULL; resp = resp->ai_next) {
    memcpy(&addrs->addrs[i].addr, resp->ai_addr, resp->ai_addrlen);
    addrs->addrs[i].len = resp->ai_addrlen;
    i++;
  }

done:
  gpr_free(host);
  gpr_free(port);
  if (result) {
    freeaddrinfo(result);
  }
  return addrs;
}

grpc_resolved_addresses *(*grpc_blocking_resolve_address)(
    const char *name, const char *default_port) = blocking_resolve_address_impl;

/* Callback to be passed to grpc_executor to asynch-ify
 * grpc_blocking_resolve_address */
static void do_request_thread(grpc_exec_ctx *exec_ctx, void *rp, bool success) {
  request *r = rp;
  grpc_resolved_addresses *resolved =
      grpc_blocking_resolve_address(r->name, r->default_port);
  void *arg = r->arg;
  grpc_resolve_cb cb = r->cb;
  gpr_free(r->name);
  gpr_free(r->default_port);
  cb(exec_ctx, arg, resolved);
  gpr_free(r);
}

void grpc_resolved_addresses_destroy(grpc_resolved_addresses *addrs) {
  gpr_free(addrs->addrs);
  gpr_free(addrs);
}

static void resolve_address_impl(grpc_exec_ctx *exec_ctx, const char *name,
                                 const char *default_port, grpc_resolve_cb cb,
                                 void *arg) {
  request *r = gpr_malloc(sizeof(request));
  grpc_closure_init(&r->request_closure, do_request_thread, r);
  r->name = gpr_strdup(name);
  r->default_port = gpr_strdup(default_port);
  r->cb = cb;
  r->arg = arg;
  grpc_executor_enqueue(&r->request_closure, 1);
}

void (*grpc_resolve_address)(grpc_exec_ctx *exec_ctx, const char *name,
                             const char *default_port, grpc_resolve_cb cb,
                             void *arg) = resolve_address_impl;

#endif
