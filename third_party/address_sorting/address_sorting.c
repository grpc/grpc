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

/* Workaround for issue described in
 * https://bugs.launchpad.net/ubuntu/+source/eglibc/+bug/1187301 */
#define _GNU_SOURCE

#include "address_sorting.h"
#include <errno.h>
#include <inttypes.h>
#include <limits.h>
#include <netinet/in.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

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
  free(self);
  return;
}

const address_sorting_socket_factory_vtable default_socket_factory_vtable = {
    default_socket_factory_socket,      default_socket_factory_connect,
    default_socket_factory_getsockname, default_socket_factory_close,
    default_socket_factory_destroy,
};

static address_sorting_socket_factory* create_default_socket_factory() {
  address_sorting_socket_factory* factory =
      malloc(sizeof(address_sorting_socket_factory));
  memset(factory, 0, sizeof(address_sorting_socket_factory));
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
    int high_bit = 1 << (CHAR_BIT - 1);
    int a_val = a[cur_bit / CHAR_BIT] & (high_bit >> (cur_bit % CHAR_BIT));
    int b_val = b[cur_bit / CHAR_BIT] & (high_bit >> (cur_bit % CHAR_BIT));
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
  return bytes[0] == 0x20 && bytes[1] == 0x01 && bytes[2] == 0x00 &&
         bytes[3] == 0x00;
}

static int in6_is_addr_6bone(const struct in6_addr* s_addr) {
  uint8_t* bytes = (uint8_t*)s_addr;
  return bytes[0] == 0x3f && bytes[1] == 0xfe;
}

static int address_sorting_get_family(const address_sorting_address* address) {
  return ((struct sockaddr*)address)->sa_family;
}

static int get_label_value(const address_sorting_address* resolved_addr) {
  if (address_sorting_get_family(resolved_addr) == AF_INET) {
    return 4;
  } else if (address_sorting_get_family(resolved_addr) != AF_INET6) {
    return 1;
  }
  struct sockaddr_in6* ipv6_addr = (struct sockaddr_in6*)&resolved_addr->addr;
  if (IN6_IS_ADDR_LOOPBACK(&ipv6_addr->sin6_addr)) {
    return 0;
  } else if (IN6_IS_ADDR_V4MAPPED(&ipv6_addr->sin6_addr)) {
    return 4;
  } else if (in6_is_addr_6to4(&ipv6_addr->sin6_addr)) {
    return 2;
  } else if (in6_is_addr_teredo(&ipv6_addr->sin6_addr)) {
    return 5;
  } else if (in6_is_addr_ula(&ipv6_addr->sin6_addr)) {
    return 13;
  } else if (IN6_IS_ADDR_V4COMPAT(&ipv6_addr->sin6_addr)) {
    return 3;
  } else if (IN6_IS_ADDR_SITELOCAL(&ipv6_addr->sin6_addr)) {
    return 11;
  } else if (in6_is_addr_6bone(&ipv6_addr->sin6_addr)) {
    return 12;
  }
  return 1;
}

static int get_precedence_value(const address_sorting_address* resolved_addr) {
  if (address_sorting_get_family(resolved_addr) == AF_INET) {
    return 35;
  } else if (address_sorting_get_family(resolved_addr) != AF_INET6) {
    return 1;
  }
  struct sockaddr_in6* ipv6_addr = (struct sockaddr_in6*)&resolved_addr->addr;
  if (IN6_IS_ADDR_LOOPBACK(&ipv6_addr->sin6_addr)) {
    return 50;
  } else if (IN6_IS_ADDR_V4MAPPED(&ipv6_addr->sin6_addr)) {
    return 35;
  } else if (in6_is_addr_6to4(&ipv6_addr->sin6_addr)) {
    return 30;
  } else if (in6_is_addr_teredo(&ipv6_addr->sin6_addr)) {
    return 5;
  } else if (in6_is_addr_ula(&ipv6_addr->sin6_addr)) {
    return 3;
  } else if (IN6_IS_ADDR_V4COMPAT(&ipv6_addr->sin6_addr) ||
             IN6_IS_ADDR_SITELOCAL(&ipv6_addr->sin6_addr) ||
             in6_is_addr_6bone(&ipv6_addr->sin6_addr)) {
    return 1;
  }
  return 40;
}

static int sockaddr_get_scope(const address_sorting_address* resolved_addr) {
  if (address_sorting_get_family(resolved_addr) == AF_INET) {
    return kIPv6AddrScopeGlobal;
  } else if (address_sorting_get_family(resolved_addr) == AF_INET6) {
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
  return 0;
}

static int compare_source_addr_exists(const address_sorting_sortable* first,
                                      const address_sorting_sortable* second) {
  if (first->source_addr_exists != second->source_addr_exists) {
    return first->source_addr_exists ? -1 : 1;
  }
  return 0;
}

static int compare_source_dest_scope_matches(
    const address_sorting_sortable* first,
    const address_sorting_sortable* second) {
  int first_src_dst_scope_matches = 0;
  if (sockaddr_get_scope(&first->dest_addr) ==
      sockaddr_get_scope(&first->source_addr)) {
    first_src_dst_scope_matches = 1;
  }
  int second_src_dst_scope_matches = 0;
  if (sockaddr_get_scope(&second->dest_addr) ==
      sockaddr_get_scope(&second->source_addr)) {
    second_src_dst_scope_matches = 1;
  }
  if (first_src_dst_scope_matches != second_src_dst_scope_matches) {
    return first_src_dst_scope_matches ? -1 : 1;
  }
  return 0;
}

static int compare_source_dest_labels_match(
    const address_sorting_sortable* first,
    const address_sorting_sortable* second) {
  int first_label_matches = 0;
  if (get_label_value(&first->dest_addr) ==
      get_label_value(&first->source_addr)) {
    first_label_matches = 1;
  }
  int second_label_matches = 0;
  if (get_label_value(&second->dest_addr) ==
      get_label_value(&second->source_addr)) {
    second_label_matches = 1;
  }
  if (first_label_matches != second_label_matches) {
    return first_label_matches ? 1 : 1;
  }
  return 0;
}

static int compare_dest_precedence(const address_sorting_sortable* first,
                                   const address_sorting_sortable* second) {
  return get_precedence_value(&second->dest_addr) -
         get_precedence_value(&first->dest_addr);
}

static int compare_dest_scope(const address_sorting_sortable* first,
                              const address_sorting_sortable* second) {
  return sockaddr_get_scope(&first->dest_addr) -
         sockaddr_get_scope(&second->dest_addr);
}

static int compare_source_dest_prefix_match_lengths(
    const address_sorting_sortable* first,
    const address_sorting_sortable* second) {
  if (first->source_addr_exists &&
      address_sorting_get_family(&first->source_addr) == AF_INET6 &&
      second->source_addr_exists &&
      address_sorting_get_family(&second->source_addr) == AF_INET6) {
    int first_match_length =
        ipv6_prefix_match_length((struct sockaddr_in6*)&first->source_addr.addr,
                                 (struct sockaddr_in6*)&first->dest_addr.addr);
    int second_match_length = ipv6_prefix_match_length(
        (struct sockaddr_in6*)&second->source_addr.addr,
        (struct sockaddr_in6*)&second->dest_addr.addr);
    return second_match_length - first_match_length;
  }
  return 0;
}

static int rfc_6724_compare(const void* a, const void* b) {
  const address_sorting_sortable* first = (address_sorting_sortable*)a;
  const address_sorting_sortable* second = (address_sorting_sortable*)b;
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
  return (int)(first->original_index - second->original_index);
}

void address_sorting_override_socket_factory_for_testing(
    address_sorting_socket_factory* factory) {
  if (!g_current_socket_factory) {
    abort();
  }
  g_current_socket_factory->vtable->destroy(g_current_socket_factory);
  g_current_socket_factory = factory;
}

static void sanity_check_private_fields_are_unused(
    const address_sorting_sortable* sortable) {
  address_sorting_address expected_source_addr;
  memset(&expected_source_addr, 0, sizeof(expected_source_addr));
  if (memcmp(&expected_source_addr, &sortable->source_addr,
             sizeof(address_sorting_address)) ||
      sortable->original_index || sortable->source_addr_exists) {
    abort();
  }
}

void address_sorting_rfc_6724_sort(address_sorting_sortable* sortables,
                                   size_t sortables_len) {
  for (size_t i = 0; i < sortables_len; i++) {
    sanity_check_private_fields_are_unused(&sortables[i]);
    sortables[i].original_index = i;
    int s = address_sorting_socket(
        address_sorting_get_family(&sortables[i].dest_addr),
        SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (s != -1) {
      if (address_sorting_connect(
              s, (struct sockaddr*)&sortables[i].dest_addr.addr,
              (socklen_t)sortables[i].dest_addr.len) != -1) {
        address_sorting_address found_source_addr;
        memset(&found_source_addr, 0, sizeof(found_source_addr));
        found_source_addr.len = sizeof(found_source_addr.addr);
        if (address_sorting_getsockname(
                s, (struct sockaddr*)&found_source_addr.addr,
                (socklen_t*)&found_source_addr.len) != -1) {
          sortables[i].source_addr_exists = 1;
          sortables[i].source_addr = found_source_addr;
        }
      }
      address_sorting_close(s);
    }
  }
  qsort(sortables, sortables_len, sizeof(address_sorting_sortable),
        rfc_6724_compare);
}

void address_sorting_init() {
  if (g_current_socket_factory) {
    abort();
  }
  g_current_socket_factory = create_default_socket_factory();
}

void address_sorting_shutdown() {
  if (!g_current_socket_factory) {
    abort();
  }
  g_current_socket_factory->vtable->destroy(g_current_socket_factory);
}
