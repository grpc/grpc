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

#ifndef _POSIX_SOURCE
#define _POSIX_SOURCE
#endif

#include "src/core/iomgr/sockaddr.h"
#include "src/core/iomgr/resolve_address.h"

#include <sys/types.h>
#include <sys/un.h>
#include <string.h>

#include "src/core/iomgr/iomgr_internal.h"
#include "src/core/iomgr/sockaddr_utils.h"
#include "src/core/support/string.h"
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/thd.h>
#include <grpc/support/time.h>

typedef struct {
  char *name;
  char *default_port;
  grpc_resolve_cb cb;
  void *arg;
} request;

static void split_host_port(const char *name, char **host, char **port) {
  const char *host_start;
  size_t host_len;
  const char *port_start;

  *host = NULL;
  *port = NULL;

  if (name[0] == '[') {
    /* Parse a bracketed host, typically an IPv6 literal. */
    const char *rbracket = strchr(name, ']');
    if (rbracket == NULL) {
      /* Unmatched [ */
      return;
    }
    if (rbracket[1] == '\0') {
      /* ]<end> */
      port_start = NULL;
    } else if (rbracket[1] == ':') {
      /* ]:<port?> */
      port_start = rbracket + 2;
    } else {
      /* ]<invalid> */
      return;
    }
    host_start = name + 1;
    host_len = rbracket - host_start;
    if (memchr(host_start, ':', host_len) == NULL) {
      /* Require all bracketed hosts to contain a colon, because a hostname or
         IPv4 address should never use brackets. */
      return;
    }
  } else {
    const char *colon = strchr(name, ':');
    if (colon != NULL && strchr(colon + 1, ':') == NULL) {
      /* Exactly 1 colon.  Split into host:port. */
      host_start = name;
      host_len = colon - name;
      port_start = colon + 1;
    } else {
      /* 0 or 2+ colons.  Bare hostname or IPv6 litearal. */
      host_start = name;
      host_len = strlen(name);
      port_start = NULL;
    }
  }

  /* Allocate return values. */
  *host = gpr_malloc(host_len + 1);
  memcpy(*host, host_start, host_len);
  (*host)[host_len] = '\0';

  if (port_start != NULL) {
    *port = gpr_strdup(port_start);
  }
}

grpc_resolved_addresses *grpc_blocking_resolve_address(
    const char *name, const char *default_port) {
  struct addrinfo hints;
  struct addrinfo *result = NULL, *resp;
  char *host;
  char *port;
  int s;
  size_t i;
  grpc_resolved_addresses *addrs = NULL;
  const gpr_timespec start_time = gpr_now();
  struct sockaddr_un *un;

  if (name[0] == 'u' && name[1] == 'n' && name[2] == 'i' && name[3] == 'x' &&
      name[4] == ':' && name[5] != 0) {
    addrs = gpr_malloc(sizeof(grpc_resolved_addresses));
    addrs->naddrs = 1;
    addrs->addrs = gpr_malloc(sizeof(grpc_resolved_address));
    un = (struct sockaddr_un *)addrs->addrs->addr;
    un->sun_family = AF_UNIX;
    strcpy(un->sun_path, name + 5);
    addrs->addrs->len = strlen(un->sun_path) + sizeof(un->sun_family);
    return addrs;
  }

  /* parse name, splitting it into host and port parts */
  split_host_port(name, &host, &port);
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

  s = getaddrinfo(host, port, &hints, &result);
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

  /* Temporary logging, to help identify flakiness in dualstack_socket_test. */
  {
    const gpr_timespec delay = gpr_time_sub(gpr_now(), start_time);
    const int delay_ms =
        delay.tv_sec * GPR_MS_PER_SEC + delay.tv_nsec / GPR_NS_PER_MS;
    gpr_log(GPR_INFO, "logspam: getaddrinfo(%s, %s) resolved %d addrs in %dms:",
            host, port, addrs->naddrs, delay_ms);
    for (i = 0; i < addrs->naddrs; i++) {
      char *buf;
      grpc_sockaddr_to_string(&buf, (struct sockaddr *)&addrs->addrs[i].addr,
                              0);
      gpr_log(GPR_INFO, "logspam:   [%d] %s", i, buf);
      gpr_free(buf);
    }
  }

done:
  gpr_free(host);
  gpr_free(port);
  if (result) {
    freeaddrinfo(result);
  }
  return addrs;
}

/* Thread function to asynch-ify grpc_blocking_resolve_address */
static void do_request(void *rp) {
  request *r = rp;
  grpc_resolved_addresses *resolved =
      grpc_blocking_resolve_address(r->name, r->default_port);
  void *arg = r->arg;
  grpc_resolve_cb cb = r->cb;
  gpr_free(r->name);
  gpr_free(r->default_port);
  gpr_free(r);
  cb(arg, resolved);
  grpc_iomgr_unref();
}

void grpc_resolved_addresses_destroy(grpc_resolved_addresses *addrs) {
  gpr_free(addrs->addrs);
  gpr_free(addrs);
}

void grpc_resolve_address(const char *name, const char *default_port,
                          grpc_resolve_cb cb, void *arg) {
  request *r = gpr_malloc(sizeof(request));
  gpr_thd_id id;
  grpc_iomgr_ref();
  r->name = gpr_strdup(name);
  r->default_port = gpr_strdup(default_port);
  r->cb = cb;
  r->arg = arg;
  gpr_thd_new(&id, do_request, r, NULL);
}