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

#ifndef GRPC_ADDRESS_SORTING_H
#define GRPC_ADDRESS_SORTING_H

#include <grpc/support/port_platform.h>

#include <netinet/in.h>
#include "src/core/ext/filters/client_channel/lb_policy_factory.h"

#ifdef __cplusplus
extern "C" {
#endif

void address_sorting_rfc_6724_sort(grpc_lb_addresses *resolved_lb_addrs);

void address_sorting_init();
void address_sorting_shutdown();

struct address_sorting_socket_factory;

/* The socket factory interface is exposed only for testing */
typedef struct {
  int (*socket)(struct address_sorting_socket_factory *factory, int domain,
                int type, int protocol);
  int (*connect)(struct address_sorting_socket_factory *factory, int sockfd,
                 const struct sockaddr *addr, socklen_t addrlen);
  int (*getsockname)(struct address_sorting_socket_factory *factory, int sockfd,
                     struct sockaddr *addr, socklen_t *addrlen);
  int (*close)(struct address_sorting_socket_factory *factory, int sockfd);
  void (*destroy)(struct address_sorting_socket_factory *factory);
} address_sorting_socket_factory_vtable;

typedef struct address_sorting_socket_factory {
  const address_sorting_socket_factory_vtable *vtable;
} address_sorting_socket_factory;

void address_sorting_override_socket_factory_for_testing(
    address_sorting_socket_factory *factory);

#ifdef __cplusplus
}
#endif

#endif  // GRPC_ADDRESS_SORTING_H
