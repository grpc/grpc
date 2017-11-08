/*	$NetBSD: getaddrinfo.c,v 1.82 2006/03/25 12:09:40 rpaulo Exp $	*/
/*	$KAME: getaddrinfo.c,v 1.29 2000/08/31 17:26:57 itojun Exp $	*/
/*
 * Copyright (C) 1995, 1996, 1997, and 1998 WIDE Project.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. Neither the name of the project nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE PROJECT AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE PROJECT OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

/*
 * This is an adaptation of Android's implementation of RFC 6724
 * (in Android's getaddrinfo.c). It has some cosmetic differences
 * from Android's getaddrinfo.c, but Android's getaddrinfo.c was
 * used as a guide or example of a way to implement the RFC 6724 spec when
 * this was written.
 */

#include "address_sorting.h"
#include <errno.h>
#include <grpc/support/alloc.h>
#include <grpc/support/port_platform.h>
#include <grpc/support/useful.h>
#include <limits.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include "src/core/ext/filters/client_channel/lb_policy_factory.h"
#include "src/core/lib/iomgr/sockaddr.h"
#include "src/core/lib/iomgr/sockaddr_utils.h"
#include "third_party/address_sorting/address_sorting.h"

// Scope values increase with increase in scope.
static const int kIPv6AddrScopeLinkLocal = 1;
const int kIPv6AddrScopeSiteLocal = 2;
const int kIPv6AddrScopeGlobal = 3;

static address_sorting_socket_factory* g_current_socket_factory = NULL;

static int default_socket_factory_socket(address_sorting_socket_factory* self,
                                         int domain, int type, int protocol) {
  return socket(domain, type, protocol);
}

static int default_socket_factory_connect(address_sorting_socket_factory* self,
                                          int sockfd,
                                          const struct sockaddr* addr,
                                          socklen_t addrlen) {
  return connect(sockfd, addr, addrlen);
}

static int default_socket_factory_getsockname(
    address_sorting_socket_factory* self, int sockfd, struct sockaddr* addr,
    socklen_t* addrlen) {
  return getsockname(sockfd, addr, addrlen);
}

static int default_socket_factory_close(address_sorting_socket_factory* self,
                                        int sockfd) {
  return close(sockfd);
}

static void default_socket_factory_destroy(
    address_sorting_socket_factory* self) {
  return;
}

const address_sorting_socket_factory_vtable default_socket_factory_vtable = {
    default_socket_factory_socket,      default_socket_factory_connect,
    default_socket_factory_getsockname, default_socket_factory_close,
    default_socket_factory_destroy,
};

static address_sorting_socket_factory* create_default_socket_factory() {
  address_sorting_socket_factory* factory =
      gpr_zalloc(sizeof(address_sorting_socket_factory));
  factory->vtable = &default_socket_factory_vtable;
  return factory;
}

static int address_sorting_socket(int domain, int type, int protocol) {
  return g_current_socket_factory->vtable->socket(g_current_socket_factory,
                                                  domain, type, protocol);
}

static int address_sorting_connect(int sockfd, const struct sockaddr* addr,
                                   socklen_t addrlen) {
  return g_current_socket_factory->vtable->connect(g_current_socket_factory,
                                                   sockfd, addr, addrlen);
}

static int address_sorting_getsockname(int sockfd, struct sockaddr* addr,
                                       socklen_t* addrlen) {
  return g_current_socket_factory->vtable->getsockname(g_current_socket_factory,
                                                       sockfd, addr, addrlen);
}

static int address_sorting_close(int sockfd) {
  return g_current_socket_factory->vtable->close(g_current_socket_factory,
                                                 sockfd);
}

static int ipv6_prefix_match_length(const struct sockaddr_in6* sa,
                                    const struct sockaddr_in6* sb) {
  unsigned char* a = (unsigned char*)&sa->sin6_addr;
  unsigned char* b = (unsigned char*)&sb->sin6_addr;
  int cur_bit = 0;
  while (cur_bit < 128) {
    int a_val = a[cur_bit / CHAR_BIT] & (1 << (cur_bit % CHAR_BIT));
    int b_val = b[cur_bit / CHAR_BIT] & (1 << (cur_bit % CHAR_BIT));
    if (a_val == b_val) {
      cur_bit++;
    } else {
      break;
    }
  }
  return cur_bit;
}

static int in6_is_addr_6to4(const struct in6_addr* s_addr) {
  uint8_t* bytes = (uint8_t*)s_addr;
  return bytes[0] == 0x20 && bytes[1] == 0x02;
}

static int in6_is_addr_ula(const struct in6_addr* s_addr) {
  uint8_t* bytes = (uint8_t*)s_addr;
  return (bytes[0] & 0xfe) == 0xfc;
}

static int in6_is_addr_teredo(const struct in6_addr* s_addr) {
  uint8_t* bytes = (uint8_t*)s_addr;
  return bytes[0] == 0x20 && bytes[1] == 0x02 && bytes[2] == 0x00 &&
         bytes[3] == 0x00;
}

static int in6_is_addr_6bone(const struct in6_addr* s_addr) {
  uint8_t* bytes = (uint8_t*)s_addr;
  return bytes[0] == 0x3f && bytes[1] == 0xfe;
}

static int get_label_value(const grpc_resolved_address* resolved_addr) {
  if (grpc_sockaddr_get_family(resolved_addr) == AF_INET) {
    return 4;
  }
  if (grpc_sockaddr_get_family(resolved_addr) != AF_INET6) {
    gpr_log(GPR_INFO, "Address is not AF_INET or AF_INET6.");
    return 1;
  }
  struct sockaddr_in6* ipv6_addr = (struct sockaddr_in6*)&resolved_addr->addr;
  if (IN6_IS_ADDR_LOOPBACK(&ipv6_addr->sin6_addr)) {
    return 0;
  }
  if (IN6_IS_ADDR_V4MAPPED(&ipv6_addr->sin6_addr)) {
    return 4;
  }
  if (in6_is_addr_6to4(&ipv6_addr->sin6_addr)) {
    return 2;
  }
  if (in6_is_addr_teredo(&ipv6_addr->sin6_addr)) {
    return 5;
  }
  if (in6_is_addr_ula(&ipv6_addr->sin6_addr)) {
    return 13;
  }
  if (IN6_IS_ADDR_V4COMPAT(&ipv6_addr->sin6_addr)) {
    return 3;
  }
  if (IN6_IS_ADDR_SITELOCAL(&ipv6_addr->sin6_addr)) {
    return 11;
  }
  if (in6_is_addr_6bone(&ipv6_addr->sin6_addr)) {
    return 12;
  }
  return 1;
}

static int get_precedence_value(const grpc_resolved_address* resolved_addr) {
  if (grpc_sockaddr_get_family(resolved_addr) == AF_INET) {
    return 35;
  }
  if (grpc_sockaddr_get_family(resolved_addr) != AF_INET6) {
    gpr_log(GPR_INFO, "Address is not AF_INET or AF_INET6.");
    return 1;
  }
  struct sockaddr_in6* ipv6_addr = (struct sockaddr_in6*)&resolved_addr->addr;
  if (IN6_IS_ADDR_LOOPBACK(&ipv6_addr->sin6_addr)) {
    return 50;
  }
  if (IN6_IS_ADDR_V4MAPPED(&ipv6_addr->sin6_addr)) {
    return 35;
  }
  if (in6_is_addr_6to4(&ipv6_addr->sin6_addr)) {
    return 30;
  }
  if (in6_is_addr_teredo(&ipv6_addr->sin6_addr)) {
    return 5;
  }
  if (in6_is_addr_ula(&ipv6_addr->sin6_addr)) {
    return 3;
  }
  if (IN6_IS_ADDR_V4COMPAT(&ipv6_addr->sin6_addr) ||
      IN6_IS_ADDR_SITELOCAL(&ipv6_addr->sin6_addr) ||
      in6_is_addr_6bone(&ipv6_addr->sin6_addr)) {
    return 1;
  }
  return 40;
}

static int sockaddr_get_scope(const grpc_resolved_address* resolved_addr) {
  if (grpc_sockaddr_get_family(resolved_addr) == AF_INET) {
    return kIPv6AddrScopeGlobal;
  }
  if (grpc_sockaddr_get_family(resolved_addr) == AF_INET6) {
    struct sockaddr_in6* ipv6_addr = (struct sockaddr_in6*)&resolved_addr->addr;
    if (IN6_IS_ADDR_LOOPBACK(&ipv6_addr->sin6_addr) ||
        IN6_IS_ADDR_LINKLOCAL(&ipv6_addr->sin6_addr)) {
      return kIPv6AddrScopeLinkLocal;
    }
    if (IN6_IS_ADDR_SITELOCAL(&ipv6_addr->sin6_addr)) {
      return kIPv6AddrScopeSiteLocal;
    }
    return kIPv6AddrScopeGlobal;
  }
  gpr_log(GPR_ERROR, "Unknown socket family %d.",
          grpc_sockaddr_get_family(resolved_addr));
  return 0;
}

typedef struct {
  grpc_lb_address lb_addr;
  grpc_resolved_address dest_addr;
  grpc_resolved_address source_addr;
  size_t original_index;
  int source_addr_exists;
} sortable_address;

static int compare_source_addr_exists(sortable_address first,
                                      sortable_address second) {
  if (first.source_addr_exists != second.source_addr_exists) {
    return first.source_addr_exists ? -1 : 1;
  }
  return 0;
}

static int compare_source_dest_scope_matches(sortable_address first,
                                            sortable_address second) {
  int first_src_dst_scope_matches = false;
  if (sockaddr_get_scope(&first.dest_addr) ==
      sockaddr_get_scope(&first.source_addr)) {
    first_src_dst_scope_matches = true;
  }
  int second_src_dst_scope_matches = false;
  if (sockaddr_get_scope(&second.dest_addr) ==
      sockaddr_get_scope(&second.source_addr)) {
    second_src_dst_scope_matches = true;
  }
  if (first_src_dst_scope_matches != second_src_dst_scope_matches) {
    return first_src_dst_scope_matches ? -1 : 1;
  }
  return 0;
}

static int compare_source_dest_labels_match(sortable_address first,
                                            sortable_address second) {
  int first_label_matches = false;
  if (get_label_value(&first.dest_addr) ==
      get_label_value(&first.source_addr)) {
    first_label_matches = true;
  }
  int second_label_matches = false;
  if (get_label_value(&second.dest_addr) ==
      get_label_value(&second.source_addr)) {
    second_label_matches = true;
  }
  if (first_label_matches != second_label_matches) {
    return first_label_matches ? 1 : 1;
  }
  return 0;
}

static int compare_dest_precedence(sortable_address first,
                                   sortable_address second) {
  return get_precedence_value(&second.dest_addr) -
         get_precedence_value(&first.dest_addr);
}

static int compare_dest_scope(sortable_address first, sortable_address second) {
  return sockaddr_get_scope(&first.dest_addr) -
         sockaddr_get_scope(&second.dest_addr);
}

static int compare_source_dest_prefix_match_lengths(sortable_address first,
                                                    sortable_address second) {
  if (first.source_addr_exists &&
      grpc_sockaddr_get_family(&first.source_addr) == AF_INET6 &&
      second.source_addr_exists &&
      grpc_sockaddr_get_family(&second.source_addr) == AF_INET6) {
    int first_match_length =
        ipv6_prefix_match_length((struct sockaddr_in6*)&first.source_addr.addr,
                                 (struct sockaddr_in6*)&first.dest_addr.addr);
    int second_match_length =
        ipv6_prefix_match_length((struct sockaddr_in6*)&second.source_addr.addr,
                                 (struct sockaddr_in6*)&second.dest_addr.addr);
    return second_match_length - first_match_length;
  }
  return 0;
}

static int rfc_6724_compare(const void* a, const void* b) {
  const sortable_address first = *(sortable_address*)a;
  const sortable_address second = *(sortable_address*)b;
  int out = 0;
  if ((out = compare_source_addr_exists(first, second))) {
    return out;
  }
  if ((out = compare_source_dest_scope_matches(first, second))) {
    return out;
  }
  if ((out = compare_source_dest_labels_match(first, second))) {
    return out;
  }
  // TODO: Implement rule 3; avoid deprecated addresses.
  // TODO: Implement rule 4; avoid temporary addresses.
  if ((out = compare_dest_precedence(first, second))) {
    return out;
  }
  // TODO: Implement rule 7; prefer native transports.
  if ((out = compare_dest_scope(first, second))) {
    return out;
  }
  if ((out = compare_source_dest_prefix_match_lengths(first, second))) {
    return out;
  }
  // Prefer that the sort be stable otherwise
  return (int)(first.original_index - second.original_index);
}

void address_sorting_override_socket_factory_for_testing(
    address_sorting_socket_factory* factory) {
  GPR_ASSERT(g_current_socket_factory);
  g_current_socket_factory->vtable->destroy(g_current_socket_factory);
  g_current_socket_factory = factory;
}

void address_sorting_rfc_6724_sort(grpc_lb_addresses* resolved_lb_addrs) {
  sortable_address* sortable =
      gpr_zalloc(sizeof(sortable_address) * resolved_lb_addrs->num_addresses);
  for (size_t i = 0; i < resolved_lb_addrs->num_addresses; i++) {
    sortable_address next;
    memset(&next, 0, sizeof(sortable_address));
    next.original_index = i;
    next.lb_addr = resolved_lb_addrs->addresses[i];
    next.dest_addr = resolved_lb_addrs->addresses[i].address;
    int address_family =
        grpc_sockaddr_get_family(&resolved_lb_addrs->addresses[i].address);
    int s =
        address_sorting_socket(address_family, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (s != -1) {
      if (address_sorting_connect(
              s,
              (struct sockaddr*)&resolved_lb_addrs->addresses[i].address.addr,
              (socklen_t)resolved_lb_addrs->addresses[i].address.len) != -1) {
        grpc_resolved_address found_source_addr;
        memset(&found_source_addr, 0, sizeof(grpc_resolved_address));
        if (address_sorting_getsockname(
                s, (struct sockaddr*)&found_source_addr.addr,
                (socklen_t*)&found_source_addr.len) != -1) {
          next.source_addr_exists = 1;
          next.source_addr = found_source_addr;
        }
      }
      address_sorting_close(s);
    }
    sortable[i] = next;
  }
  qsort(sortable, resolved_lb_addrs->num_addresses, sizeof(sortable_address),
        rfc_6724_compare);
  grpc_lb_address* sorted_lb_addrs = (grpc_lb_address*)gpr_zalloc(
      resolved_lb_addrs->num_addresses * sizeof(grpc_lb_address));
  for (size_t i = 0; i < resolved_lb_addrs->num_addresses; i++) {
    sorted_lb_addrs[i] = sortable[i].lb_addr;
  }
  gpr_free(sortable);
  gpr_free(resolved_lb_addrs->addresses);
  resolved_lb_addrs->addresses = sorted_lb_addrs;
}

void address_sorting_init() {
  g_current_socket_factory = create_default_socket_factory();
}

void address_sorting_shutdown() {
  GPR_ASSERT(g_current_socket_factory != NULL);
  g_current_socket_factory->vtable->destroy(g_current_socket_factory);
}
