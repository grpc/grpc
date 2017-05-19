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
#if GRPC_ARES == 1 && !defined(GRPC_UV)

#include "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_wrapper.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/iomgr/socket_utils_posix.h"

#include <string.h>
#include <sys/types.h>

#include <ares.h>
#include <grpc/support/alloc.h>
#include <grpc/support/host_port.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/time.h>
#include <grpc/support/useful.h>

#include "src/core/ext/filters/client_channel/parse_address.h"
#include "src/core/ext/filters/client_channel/resolver/dns/c_ares/grpc_ares_ev_driver.h"
#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/executor.h"
#include "src/core/lib/iomgr/iomgr_internal.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"
#include "src/core/lib/support/string.h"

static gpr_once g_basic_init = GPR_ONCE_INIT;
static gpr_mu g_init_mu;

typedef struct grpc_ares_request {
  /** indicates the DNS server to use, if specified */
  struct ares_addr_port_node dns_server_addr;
  /** following members are set in grpc_resolve_address_ares_impl */
  /** host to resolve, parsed from the name to resolve */
  char *host;
  /** port to fill in sockaddr_in, parsed from the name to resolve */
  char *port;
  /** default port to use */
  char *default_port;
  /** closure to call when the request completes */
  grpc_closure *on_done;
  /** the pointer to receive the resolved addresses */
  grpc_resolved_addresses **addrs_out;
  /** the evernt driver used by this request */
  grpc_ares_ev_driver *ev_driver;
  /** number of ongoing queries */
  gpr_refcount pending_queries;

  /** mutex guarding the rest of the state */
  gpr_mu mu;
  /** is there at least one successful query, set in on_done_cb */
  bool success;
  /** the errors explaining the request failure, set in on_done_cb */
  grpc_error *error;
} grpc_ares_request;

static void do_basic_init(void) { gpr_mu_init(&g_init_mu); }

static uint16_t strhtons(const char *port) {
  if (strcmp(port, "http") == 0) {
    return htons(80);
  } else if (strcmp(port, "https") == 0) {
    return htons(443);
  }
  return htons((unsigned short)atoi(port));
}

static void grpc_ares_request_unref(grpc_exec_ctx *exec_ctx,
                                    grpc_ares_request *r) {
  /* If there are no pending queries, invoke on_done callback and destroy the
     request */
  if (gpr_unref(&r->pending_queries)) {
    /* TODO(zyc): Sort results with RFC6724 before invoking on_done. */
    if (exec_ctx == NULL) {
      /* A new exec_ctx is created here, as the c-ares interface does not
         provide one in ares_host_callback. It's safe to schedule on_done with
         the newly created exec_ctx, since the caller has been warned not to
         acquire locks in on_done. ares_dns_resolver is using combiner to
         protect resources needed by on_done. */
      grpc_exec_ctx new_exec_ctx = GRPC_EXEC_CTX_INIT;
      grpc_closure_sched(&new_exec_ctx, r->on_done, r->error);
      grpc_exec_ctx_finish(&new_exec_ctx);
    } else {
      grpc_closure_sched(exec_ctx, r->on_done, r->error);
    }
    gpr_mu_destroy(&r->mu);
    grpc_ares_ev_driver_destroy(r->ev_driver);
    gpr_free(r->host);
    gpr_free(r->port);
    gpr_free(r->default_port);
    gpr_free(r);
  }
}

static void on_done_cb(void *arg, int status, int timeouts,
                       struct hostent *hostent) {
  grpc_ares_request *r = (grpc_ares_request *)arg;
  gpr_mu_lock(&r->mu);
  if (status == ARES_SUCCESS) {
    GRPC_ERROR_UNREF(r->error);
    r->error = GRPC_ERROR_NONE;
    r->success = true;
    grpc_resolved_addresses **addresses = r->addrs_out;
    if (*addresses == NULL) {
      *addresses = gpr_malloc(sizeof(grpc_resolved_addresses));
      (*addresses)->naddrs = 0;
      (*addresses)->addrs = NULL;
    }
    size_t prev_naddr = (*addresses)->naddrs;
    size_t i;
    for (i = 0; hostent->h_addr_list[i] != NULL; i++) {
    }
    (*addresses)->naddrs += i;
    (*addresses)->addrs =
        gpr_realloc((*addresses)->addrs,
                    sizeof(grpc_resolved_address) * (*addresses)->naddrs);
    for (i = prev_naddr; i < (*addresses)->naddrs; i++) {
      memset(&(*addresses)->addrs[i], 0, sizeof(grpc_resolved_address));
      if (hostent->h_addrtype == AF_INET6) {
        (*addresses)->addrs[i].len = sizeof(struct sockaddr_in6);
        struct sockaddr_in6 *addr =
            (struct sockaddr_in6 *)&(*addresses)->addrs[i].addr;
        addr->sin6_family = (sa_family_t)hostent->h_addrtype;
        addr->sin6_port = strhtons(r->port);

        char output[INET6_ADDRSTRLEN];
        memcpy(&addr->sin6_addr, hostent->h_addr_list[i - prev_naddr],
               sizeof(struct in6_addr));
        ares_inet_ntop(AF_INET6, &addr->sin6_addr, output, INET6_ADDRSTRLEN);
        gpr_log(GPR_DEBUG,
                "c-ares resolver gets a AF_INET6 result: \n"
                "  addr: %s\n  port: %s\n  sin6_scope_id: %d\n",
                output, r->port, addr->sin6_scope_id);
      } else {
        (*addresses)->addrs[i].len = sizeof(struct sockaddr_in);
        struct sockaddr_in *addr =
            (struct sockaddr_in *)&(*addresses)->addrs[i].addr;
        memcpy(&addr->sin_addr, hostent->h_addr_list[i - prev_naddr],
               sizeof(struct in_addr));
        addr->sin_family = (sa_family_t)hostent->h_addrtype;
        addr->sin_port = strhtons(r->port);

        char output[INET_ADDRSTRLEN];
        ares_inet_ntop(AF_INET, &addr->sin_addr, output, INET_ADDRSTRLEN);
        gpr_log(GPR_DEBUG,
                "c-ares resolver gets a AF_INET result: \n"
                "  addr: %s\n  port: %s\n",
                output, r->port);
      }
    }
  } else if (!r->success) {
    char *error_msg;
    gpr_asprintf(&error_msg, "C-ares status is not ARES_SUCCESS: %s",
                 ares_strerror(status));
    grpc_error *error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(error_msg);
    gpr_free(error_msg);
    if (r->error == GRPC_ERROR_NONE) {
      r->error = error;
    } else {
      r->error = grpc_error_add_child(error, r->error);
    }
  }
  gpr_mu_unlock(&r->mu);
  grpc_ares_request_unref(NULL, r);
}

void grpc_dns_lookup_ares(grpc_exec_ctx *exec_ctx, const char *dns_server,
                          const char *name, const char *default_port,
                          grpc_pollset_set *interested_parties,
                          grpc_closure *on_done,
                          grpc_resolved_addresses **addrs) {
  grpc_error *error = GRPC_ERROR_NONE;
  /* TODO(zyc): Enable tracing after #9603 is checked in */
  /* if (grpc_dns_trace) {
      gpr_log(GPR_DEBUG, "resolve_address (blocking): name=%s, default_port=%s",
              name, default_port);
     } */

  /* parse name, splitting it into host and port parts */
  char *host;
  char *port;
  gpr_split_host_port(name, &host, &port);
  if (host == NULL) {
    error = grpc_error_set_str(
        GRPC_ERROR_CREATE_FROM_STATIC_STRING("unparseable host:port"),
        GRPC_ERROR_STR_TARGET_ADDRESS, grpc_slice_from_copied_string(name));
    goto error_cleanup;
  } else if (port == NULL) {
    if (default_port == NULL) {
      error = grpc_error_set_str(
          GRPC_ERROR_CREATE_FROM_STATIC_STRING("no port in name"),
          GRPC_ERROR_STR_TARGET_ADDRESS, grpc_slice_from_copied_string(name));
      goto error_cleanup;
    }
    port = gpr_strdup(default_port);
  }

  grpc_ares_ev_driver *ev_driver;
  error = grpc_ares_ev_driver_create(&ev_driver, interested_parties);
  if (error != GRPC_ERROR_NONE) goto error_cleanup;

  grpc_ares_request *r = gpr_malloc(sizeof(grpc_ares_request));
  gpr_mu_init(&r->mu);
  r->ev_driver = ev_driver;
  r->on_done = on_done;
  r->addrs_out = addrs;
  r->default_port = gpr_strdup(default_port);
  r->port = port;
  r->host = host;
  r->success = false;
  r->error = GRPC_ERROR_NONE;
  ares_channel *channel = grpc_ares_ev_driver_get_channel(r->ev_driver);

  // If dns_server is specified, use it.
  if (dns_server != NULL) {
    gpr_log(GPR_INFO, "Using DNS server %s", dns_server);
    grpc_resolved_address addr;
    if (grpc_parse_ipv4_hostport(dns_server, &addr, false /* log_errors */)) {
      r->dns_server_addr.family = AF_INET;
      memcpy(&r->dns_server_addr.addr.addr4, addr.addr, addr.len);
      r->dns_server_addr.tcp_port = grpc_sockaddr_get_port(&addr);
      r->dns_server_addr.udp_port = grpc_sockaddr_get_port(&addr);
    } else if (grpc_parse_ipv6_hostport(dns_server, &addr,
                                        false /* log_errors */)) {
      r->dns_server_addr.family = AF_INET6;
      memcpy(&r->dns_server_addr.addr.addr6, addr.addr, addr.len);
      r->dns_server_addr.tcp_port = grpc_sockaddr_get_port(&addr);
      r->dns_server_addr.udp_port = grpc_sockaddr_get_port(&addr);
    } else {
      error = grpc_error_set_str(
          GRPC_ERROR_CREATE_FROM_STATIC_STRING("cannot parse authority"),
          GRPC_ERROR_STR_TARGET_ADDRESS, grpc_slice_from_copied_string(name));
      goto error_cleanup;
    }
    int status = ares_set_servers_ports(*channel, &r->dns_server_addr);
    if (status != ARES_SUCCESS) {
      char *error_msg;
      gpr_asprintf(&error_msg, "C-ares status is not ARES_SUCCESS: %s",
                   ares_strerror(status));
      error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(error_msg);
      gpr_free(error_msg);
      goto error_cleanup;
    }
  }
  // An extra reference is put here to avoid destroying the request in
  // on_done_cb before calling grpc_ares_ev_driver_start.
  gpr_ref_init(&r->pending_queries, 2);
  if (grpc_ipv6_loopback_available()) {
    gpr_ref(&r->pending_queries);
    ares_gethostbyname(*channel, r->host, AF_INET6, on_done_cb, r);
  }
  ares_gethostbyname(*channel, r->host, AF_INET, on_done_cb, r);
  /* TODO(zyc): Handle CNAME records here. */
  grpc_ares_ev_driver_start(exec_ctx, r->ev_driver);
  grpc_ares_request_unref(exec_ctx, r);
  return;

error_cleanup:
  grpc_closure_sched(exec_ctx, on_done, error);
  gpr_free(host);
  gpr_free(port);
}

void grpc_resolve_address_ares_impl(grpc_exec_ctx *exec_ctx, const char *name,
                                    const char *default_port,
                                    grpc_pollset_set *interested_parties,
                                    grpc_closure *on_done,
                                    grpc_resolved_addresses **addrs) {
  grpc_dns_lookup_ares(exec_ctx, NULL /* dns_server */, name, default_port,
                       interested_parties, on_done, addrs);
}

void (*grpc_resolve_address_ares)(
    grpc_exec_ctx *exec_ctx, const char *name, const char *default_port,
    grpc_pollset_set *interested_parties, grpc_closure *on_done,
    grpc_resolved_addresses **addrs) = grpc_resolve_address_ares_impl;

grpc_error *grpc_ares_init(void) {
  gpr_once_init(&g_basic_init, do_basic_init);
  gpr_mu_lock(&g_init_mu);
  int status = ares_library_init(ARES_LIB_INIT_ALL);
  gpr_mu_unlock(&g_init_mu);

  if (status != ARES_SUCCESS) {
    char *error_msg;
    gpr_asprintf(&error_msg, "ares_library_init failed: %s",
                 ares_strerror(status));
    grpc_error *error = GRPC_ERROR_CREATE_FROM_COPIED_STRING(error_msg);
    gpr_free(error_msg);
    return error;
  }
  return GRPC_ERROR_NONE;
}

void grpc_ares_cleanup(void) {
  gpr_mu_lock(&g_init_mu);
  ares_library_cleanup();
  gpr_mu_unlock(&g_init_mu);
}

#endif /* GRPC_ARES == 1 && !defined(GRPC_UV) */
