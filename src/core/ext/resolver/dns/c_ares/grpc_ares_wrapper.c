/*
 *
 * Copyright 2016, Google Inc.
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
#ifndef GRPC_NATIVE_ADDRESS_RESOLVE

#include "src/core/ext/resolver/dns/c_ares/grpc_ares_wrapper.h"
#include "src/core/lib/iomgr/sockaddr.h"

#include <string.h>
#include <sys/types.h>

#include <ares.h>
#include <grpc/support/alloc.h>
#include <grpc/support/host_port.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/thd.h>
#include <grpc/support/time.h>
#include <grpc/support/useful.h>
#include "src/core/ext/resolver/dns/c_ares/grpc_ares_ev_driver.h"
#include "src/core/lib/iomgr/executor.h"
#include "src/core/lib/iomgr/iomgr_internal.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"
#include "src/core/lib/iomgr/unix_sockets_posix.h"
#include "src/core/lib/support/block_annotate.h"
#include "src/core/lib/support/string.h"

static gpr_once g_basic_init = GPR_ONCE_INIT;
static gpr_mu g_init_mu;

typedef struct grpc_ares_request {
  char *name;
  char *host;
  char *port;
  char *default_port;
  grpc_polling_entity *pollent;
  grpc_closure *on_done;
  grpc_resolved_addresses **addrs_out;
  grpc_closure request_closure;
  void *arg;
  int pending_quries;
  int success;
  grpc_error *error;
  grpc_ares_ev_driver *ev_driver;
} grpc_ares_request;

static void do_basic_init(void) { gpr_mu_init(&g_init_mu); }

static void destroy_request(grpc_ares_request *request) {
  grpc_ares_ev_driver_destroy(request->ev_driver);

  // ares_cancel(request->channel);
  // ares_destroy(request->channel);
  gpr_free(request->name);
  gpr_free(request->host);
  gpr_free(request->port);
  gpr_free(request->default_port);
}

static uint16_t strhtons(const char *port) {
  if (strcmp(port, "http") == 0) {
    return htons(80);
  } else if (strcmp(port, "https") == 0) {
    return htons(443);
  }
  return htons((unsigned short)atoi(port));
}

static void on_done_cb(void *arg, int status, int timeouts,
                       struct hostent *hostent) {
  gpr_log(GPR_ERROR, "status: %d", status);
  grpc_ares_request *r = (grpc_ares_request *)arg;
  gpr_log(GPR_ERROR, "status: %s", r->name);
  grpc_resolved_addresses **addresses = r->addrs_out;
  size_t i;
  size_t prev_naddr;

  if (status == ARES_SUCCESS) {
    gpr_log(GPR_ERROR, "status ARES_SUCCESS");
    GRPC_ERROR_UNREF(r->error);
    r->error = GRPC_ERROR_NONE;
    r->success = 1;
    if (*addresses == NULL) {
      *addresses = gpr_malloc(sizeof(grpc_resolved_addresses));
      (*addresses)->naddrs = 0;
      (*addresses)->addrs = NULL;
    }

    prev_naddr = (*addresses)->naddrs;
    for (i = 0; hostent->h_addr_list[i] != NULL; i++) {
    }
    (*addresses)->naddrs += i;

    gpr_log(GPR_ERROR, "naddr: %" PRIuPTR, (*addresses)->naddrs);
    (*addresses)->addrs =
        gpr_realloc((*addresses)->addrs,
                    sizeof(grpc_resolved_address) * (*addresses)->naddrs);

    for (i = prev_naddr; i < (*addresses)->naddrs; i++) {
      memset(&(*addresses)->addrs[i], 0, sizeof(grpc_resolved_address));
      if (hostent->h_addrtype == AF_INET6) {
        char output[INET6_ADDRSTRLEN];
        gpr_log(GPR_ERROR, "AF_INET6");
        struct sockaddr_in6 *addr;

        (*addresses)->addrs[i].len = sizeof(struct sockaddr_in6);
        addr = (struct sockaddr_in6 *)&(*addresses)->addrs[i].addr;

        memcpy(&addr->sin6_addr, hostent->h_addr_list[i - prev_naddr],
               sizeof(struct in6_addr));
        ares_inet_ntop(AF_INET6, &addr->sin6_addr, output, INET6_ADDRSTRLEN);
        gpr_log(GPR_ERROR, "addr: %s", output);
        gpr_log(GPR_ERROR, "port: %s", r->port);
        addr->sin6_family = (sa_family_t)hostent->h_addrtype;
        addr->sin6_port = strhtons(r->port);
      } else {
        char output[INET_ADDRSTRLEN];
        gpr_log(GPR_ERROR, "AF_INET");
        struct sockaddr_in *addr;

        (*addresses)->addrs[i].len = sizeof(struct sockaddr_in);
        addr = (struct sockaddr_in *)&(*addresses)->addrs[i].addr;

        memcpy(&addr->sin_addr, hostent->h_addr_list[i - prev_naddr],
          sizeof(struct in_addr));
        ares_inet_ntop(AF_INET, &addr->sin_addr, output, INET_ADDRSTRLEN);
        gpr_log(GPR_ERROR, "addr: %s", output);
        gpr_log(GPR_ERROR, "port: %s", r->port);
        addr->sin_family = (sa_family_t)hostent->h_addrtype;
        addr->sin_port = strhtons(r->port);
      }
    }
    // ares_destroy(r->channel);
  } else if (!r->success) {
    gpr_log(GPR_ERROR, "status not ARES_SUCCESS");
    // TODO(zyc): add more error detail
    if (r->error == GRPC_ERROR_NONE) {
      r->error = GRPC_ERROR_CREATE("C-ares query error");
    }
  }
  if (--r->pending_quries == 0) {
    grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;
    grpc_exec_ctx_sched(&exec_ctx, r->on_done, r->error, NULL);
    grpc_exec_ctx_flush(&exec_ctx);
    grpc_exec_ctx_finish(&exec_ctx);

    destroy_request(r);
    gpr_free(r);
  }
}

static void request_resolving_address(grpc_exec_ctx *exec_ctx, void *arg,
                                      grpc_error *error) {
  grpc_ares_request *r = (grpc_ares_request *)arg;
  grpc_ares_ev_driver *ev_driver = r->ev_driver;
  ares_channel *channel = grpc_ares_ev_driver_get_channel(ev_driver);
  gpr_log(GPR_ERROR, "before ares_gethostbyname %s", r->host);
  r->pending_quries = 2;
  ares_gethostbyname(*channel, r->host, AF_INET, on_done_cb, r);
  ares_gethostbyname(*channel, r->host, AF_INET6, on_done_cb, r);
  // grpc_ares_gethostbyname(r->ev_driver, r->host, on_dones_cb, r);
  gpr_log(GPR_ERROR, "before ares_getsock");
  grpc_ares_notify_on_event(exec_ctx, ev_driver);
  gpr_log(GPR_ERROR, "eof resolve_address_impl");
}

static int try_fake_resolve(const char *name, const char *port,
                            grpc_resolved_addresses **addresses) {
  struct sockaddr_in sa;
  struct sockaddr_in6 sa6;
  memset(&sa, 0, sizeof(struct sockaddr_in));
  memset(&sa6, 0, sizeof(struct sockaddr_in6));
  if (0 != ares_inet_pton(AF_INET, name, &(sa.sin_addr))) {
    gpr_log(GPR_ERROR, "AF_INET");
    *addresses = gpr_malloc(sizeof(grpc_resolved_addresses));
    (*addresses)->naddrs = 1;
    (*addresses)->addrs =
        gpr_malloc(sizeof(grpc_resolved_address) * (*addresses)->naddrs);
    (*addresses)->addrs[0].len = sizeof(struct sockaddr_in);
    sa.sin_family = AF_INET;
    sa.sin_port = strhtons(port);
    memcpy(&(*addresses)->addrs[0].addr, &sa, sizeof(struct sockaddr_in));
    return 1;
  }
  if (0 != ares_inet_pton(AF_INET6, name, &(sa6.sin6_addr))) {
    char output[INET6_ADDRSTRLEN];
    gpr_log(GPR_ERROR, "AF_INET6");
    *addresses = gpr_malloc(sizeof(grpc_resolved_addresses));
    (*addresses)->naddrs = 1;
    (*addresses)->addrs =
        gpr_malloc(sizeof(grpc_resolved_address) * (*addresses)->naddrs);
    (*addresses)->addrs[0].len = sizeof(struct sockaddr_in6);
    sa6.sin6_family = AF_INET6;
    sa6.sin6_port = strhtons(port);
    memcpy(&(*addresses)->addrs[0].addr, &sa6, sizeof(struct sockaddr_in6));
    ares_inet_ntop(AF_INET6, &sa6.sin6_addr, output, INET6_ADDRSTRLEN);
    gpr_log(GPR_ERROR, "addr: %s", output);
    gpr_log(GPR_ERROR, "port: %s", port);

    return 1;
  }
  return 0;
}

void grpc_resolve_address_ares_impl(grpc_exec_ctx *exec_ctx, const char *name,
                                    const char *default_port,
                                    grpc_pollset_set *pollset_set,
                                    grpc_closure *on_done,
                                    grpc_resolved_addresses **addrs) {
  char *host;
  char *port;
  grpc_error *err;
  grpc_ares_request *r = NULL;
  grpc_ares_ev_driver *ev_driver;

  if ((err = grpc_customized_resolve_address(name, default_port, addrs)) !=
      GRPC_ERROR_CANCELLED) {
    grpc_exec_ctx_sched(exec_ctx, on_done, err, NULL);
    return;
  }

  /* parse name, splitting it into host and port parts */
  gpr_split_host_port(name, &host, &port);
  if (host == NULL) {
    err = grpc_error_set_str(GRPC_ERROR_CREATE("unparseable host:port"),
                             GRPC_ERROR_STR_TARGET_ADDRESS, name);
    grpc_exec_ctx_sched(exec_ctx, on_done, err, NULL);
    goto done;
  } else if (port == NULL) {
    if (default_port == NULL) {
      err = grpc_error_set_str(GRPC_ERROR_CREATE("no port in name"),
                               GRPC_ERROR_STR_TARGET_ADDRESS, name);
      grpc_exec_ctx_sched(exec_ctx, on_done, err, NULL);
      goto done;
    }
    port = gpr_strdup(default_port);
  }

  if (try_fake_resolve(host, port, addrs)) {
    grpc_exec_ctx_sched(exec_ctx, on_done, GRPC_ERROR_NONE, NULL);
  } else {
    err = grpc_ares_ev_driver_create(&ev_driver, pollset_set);
    if (err != GRPC_ERROR_NONE) {
      grpc_exec_ctx_sched(exec_ctx, on_done, err, NULL);
      return;
    }
    r = gpr_malloc(sizeof(grpc_ares_request));
    r->ev_driver = ev_driver;
    r->on_done = on_done;
    r->addrs_out = addrs;
    r->name = gpr_strdup(name);
    r->default_port = gpr_strdup(default_port);
    r->port = gpr_strdup(port);
    r->host = gpr_strdup(host);
    r->pending_quries = 0;
    r->success = 0;
    r->error = GRPC_ERROR_NONE;
    grpc_closure_init(&r->request_closure, request_resolving_address, r);
    grpc_exec_ctx_sched(exec_ctx, &r->request_closure, GRPC_ERROR_NONE, NULL);
  }

done:
  gpr_free(host);
  gpr_free(port);
}

void (*grpc_resolve_address_ares)(
    grpc_exec_ctx *exec_ctx, const char *name, const char *default_port,
    grpc_pollset_set *pollset_set, grpc_closure *on_done,
    grpc_resolved_addresses **addrs) = grpc_resolve_address_ares_impl;

grpc_error *grpc_ares_init(void) {
  gpr_once_init(&g_basic_init, do_basic_init);
  gpr_mu_lock(&g_init_mu);
  int status = ares_library_init(ARES_LIB_INIT_ALL);
  gpr_mu_unlock(&g_init_mu);

  if (status != ARES_SUCCESS) {
    return GRPC_ERROR_CREATE("ares_library_init failed");
  }
  return GRPC_ERROR_NONE;
}

void grpc_ares_cleanup(void) {
  gpr_mu_lock(&g_init_mu);
  ares_library_cleanup();
  gpr_mu_unlock(&g_init_mu);
}

#endif /* GRPC_NATIVE_ADDRESS_RESOLVE */
