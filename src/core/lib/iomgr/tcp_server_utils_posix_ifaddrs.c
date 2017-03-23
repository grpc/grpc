/*
 *
 * Copyright 2017, Google Inc.
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

#ifdef GRPC_HAVE_IFADDRS

#include "src/core/lib/iomgr/tcp_server_utils_posix.h"

#include <errno.h>
#include <ifaddrs.h>
#include <stddef.h>
#include <string.h>

#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>

#include "src/core/lib/iomgr/error.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"

/* Return the listener in s with address addr or NULL. */
static grpc_tcp_listener *find_listener_with_addr(grpc_tcp_server *s,
                                                  grpc_resolved_address *addr) {
  grpc_tcp_listener *l;
  gpr_mu_lock(&s->mu);
  for (l = s->head; l != NULL; l = l->next) {
    if (l->addr.len != addr->len) {
      continue;
    }
    if (memcmp(l->addr.addr, addr->addr, addr->len) == 0) {
      break;
    }
  }
  gpr_mu_unlock(&s->mu);
  return l;
}

/* Bind to "::" to get a port number not used by any address. */
static grpc_error *get_unused_port(int *port) {
  grpc_resolved_address wild;
  grpc_sockaddr_make_wildcard6(0, &wild);
  grpc_dualstack_mode dsmode;
  int fd;
  grpc_error *err =
      grpc_create_dualstack_socket(&wild, SOCK_STREAM, 0, &dsmode, &fd);
  if (err != GRPC_ERROR_NONE) {
    return err;
  }
  if (dsmode == GRPC_DSMODE_IPV4) {
    grpc_sockaddr_make_wildcard4(0, &wild);
  }
  if (bind(fd, (const struct sockaddr *)wild.addr, (socklen_t)wild.len) != 0) {
    err = GRPC_OS_ERROR(errno, "bind");
    close(fd);
    return err;
  }
  if (getsockname(fd, (struct sockaddr *)wild.addr, (socklen_t *)&wild.len) !=
      0) {
    err = GRPC_OS_ERROR(errno, "getsockname");
    close(fd);
    return err;
  }
  close(fd);
  *port = grpc_sockaddr_get_port(&wild);
  return *port <= 0 ? GRPC_ERROR_CREATE_FROM_STATIC_STRING("Bad port")
                    : GRPC_ERROR_NONE;
}

grpc_error *grpc_tcp_server_add_all_local_addrs(grpc_tcp_server *s,
                                                unsigned port_index,
                                                int requested_port,
                                                int *out_port) {
  struct ifaddrs *ifa = NULL;
  struct ifaddrs *ifa_it;
  unsigned fd_index = 0;
  grpc_tcp_listener *sp = NULL;
  grpc_error *err = GRPC_ERROR_NONE;
  if (requested_port == 0) {
    /* Note: There could be a race where some local addrs can listen on the
       selected port and some can't. The sane way to handle this would be to
       retry by recreating the whole grpc_tcp_server. Backing out individual
       listeners and orphaning the FDs looks like too much trouble. */
    if ((err = get_unused_port(&requested_port)) != GRPC_ERROR_NONE) {
      return err;
    } else if (requested_port <= 0) {
      return GRPC_ERROR_CREATE_FROM_STATIC_STRING("Bad get_unused_port()");
    }
    gpr_log(GPR_DEBUG, "Picked unused port %d", requested_port);
  }
  if (getifaddrs(&ifa) != 0 || ifa == NULL) {
    return GRPC_OS_ERROR(errno, "getifaddrs");
  }
  for (ifa_it = ifa; ifa_it != NULL; ifa_it = ifa_it->ifa_next) {
    grpc_resolved_address addr;
    char *addr_str = NULL;
    grpc_dualstack_mode dsmode;
    grpc_tcp_listener *new_sp = NULL;
    const char *ifa_name = (ifa_it->ifa_name ? ifa_it->ifa_name : "<unknown>");
    if (ifa_it->ifa_addr == NULL) {
      continue;
    } else if (ifa_it->ifa_addr->sa_family == AF_INET) {
      addr.len = sizeof(struct sockaddr_in);
    } else if (ifa_it->ifa_addr->sa_family == AF_INET6) {
      addr.len = sizeof(struct sockaddr_in6);
    } else {
      continue;
    }
    memcpy(addr.addr, ifa_it->ifa_addr, addr.len);
    if (!grpc_sockaddr_set_port(&addr, requested_port)) {
      /* Should never happen, because we check sa_family above. */
      err = GRPC_ERROR_CREATE_FROM_STATIC_STRING("Failed to set port");
      break;
    }
    if (grpc_sockaddr_to_string(&addr_str, &addr, 0) < 0) {
      addr_str = gpr_strdup("<error>");
    }
    gpr_log(GPR_DEBUG,
            "Adding local addr from interface %s flags 0x%x to server: %s",
            ifa_name, ifa_it->ifa_flags, addr_str);
    /* We could have multiple interfaces with the same address (e.g., bonding),
       so look for duplicates. */
    if (find_listener_with_addr(s, &addr) != NULL) {
      gpr_log(GPR_DEBUG, "Skipping duplicate addr %s on interface %s", addr_str,
              ifa_name);
      gpr_free(addr_str);
      continue;
    }
    if ((err = grpc_tcp_server_add_addr(s, &addr, port_index, fd_index, &dsmode,
                                        &new_sp)) != GRPC_ERROR_NONE) {
      char *err_str = NULL;
      grpc_error *root_err;
      if (gpr_asprintf(&err_str, "Failed to add listener: %s", addr_str) < 0) {
        err_str = gpr_strdup("Failed to add listener");
      }
      root_err = GRPC_ERROR_CREATE_FROM_COPIED_STRING(err_str);
      gpr_free(err_str);
      gpr_free(addr_str);
      err = grpc_error_add_child(root_err, err);
      break;
    } else {
      GPR_ASSERT(requested_port == new_sp->port);
      ++fd_index;
      if (sp != NULL) {
        new_sp->is_sibling = 1;
        sp->sibling = new_sp;
      }
      sp = new_sp;
    }
    gpr_free(addr_str);
  }
  freeifaddrs(ifa);
  if (err != GRPC_ERROR_NONE) {
    return err;
  } else if (sp == NULL) {
    return GRPC_ERROR_CREATE_FROM_STATIC_STRING("No local addresses");
  } else {
    *out_port = sp->port;
    return GRPC_ERROR_NONE;
  }
}

bool grpc_tcp_server_have_ifaddrs(void) { return true; }

#endif /* GRPC_HAVE_IFADDRS */
