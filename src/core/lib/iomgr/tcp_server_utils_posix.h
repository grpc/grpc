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

#ifndef GRPC_CORE_LIB_IOMGR_TCP_SERVER_UTILS_POSIX_H
#define GRPC_CORE_LIB_IOMGR_TCP_SERVER_UTILS_POSIX_H

#include "src/core/lib/iomgr/ev_posix.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/socket_utils_posix.h"
#include "src/core/lib/iomgr/tcp_server.h"

/* one listening port */
typedef struct grpc_tcp_listener {
  int fd;
  grpc_fd *emfd;
  grpc_tcp_server *server;
  grpc_resolved_address addr;
  int port;
  unsigned port_index;
  unsigned fd_index;
  grpc_closure read_closure;
  grpc_closure destroyed_closure;
  struct grpc_tcp_listener *next;
  /* sibling is a linked list of all listeners for a given port. add_port and
     clone_port place all new listeners in the same sibling list. A member of
     the 'sibling' list is also a member of the 'next' list. The head of each
     sibling list has is_sibling==0, and subsequent members of sibling lists
     have is_sibling==1. is_sibling allows separate sibling lists to be
     identified while iterating through 'next'. */
  struct grpc_tcp_listener *sibling;
  int is_sibling;
} grpc_tcp_listener;

/* the overall server */
struct grpc_tcp_server {
  gpr_refcount refs;
  /* Called whenever accept() succeeds on a server port. */
  grpc_tcp_server_cb on_accept_cb;
  void *on_accept_cb_arg;

  gpr_mu mu;

  /* active port count: how many ports are actually still listening */
  size_t active_ports;
  /* destroyed port count: how many ports are completely destroyed */
  size_t destroyed_ports;

  /* is this server shutting down? */
  bool shutdown;
  /* have listeners been shutdown? */
  bool shutdown_listeners;
  /* use SO_REUSEPORT */
  bool so_reuseport;
  /* expand wildcard addresses to a list of all local addresses */
  bool expand_wildcard_addrs;

  /* linked list of server ports */
  grpc_tcp_listener *head;
  grpc_tcp_listener *tail;
  unsigned nports;

  /* List of closures passed to shutdown_starting_add(). */
  grpc_closure_list shutdown_starting;

  /* shutdown callback */
  grpc_closure *shutdown_complete;

  /* all pollsets interested in new connections */
  grpc_pollset **pollsets;
  /* number of pollsets in the pollsets array */
  size_t pollset_count;

  /* next pollset to assign a channel to */
  gpr_atm next_pollset_to_assign;

  /* channel args for this server */
  grpc_channel_args *channel_args;
};

/* If successful, add a listener to \a s for \a addr, set \a dsmode for the
   socket, and return the \a listener. */
grpc_error *grpc_tcp_server_add_addr(grpc_tcp_server *s,
                                     const grpc_resolved_address *addr,
                                     unsigned port_index, unsigned fd_index,
                                     grpc_dualstack_mode *dsmode,
                                     grpc_tcp_listener **listener);

/* Get all addresses assigned to network interfaces on the machine and create a
   listener for each. requested_port is the port to use for every listener, or 0
   to select one random port that will be used for every listener. Set *out_port
   to the port selected. Return GRPC_ERROR_NONE only if all listeners were
   added. */
grpc_error *grpc_tcp_server_add_all_local_addrs(grpc_tcp_server *s,
                                                unsigned port_index,
                                                int requested_port,
                                                int *out_port);

/* Prepare a recently-created socket for listening. */
grpc_error *grpc_tcp_server_prepare_socket(int fd,
                                           const grpc_resolved_address *addr,
                                           bool so_reuseport, int *port);
/* Ruturn true if the platform supports ifaddrs */
bool grpc_tcp_server_have_ifaddrs(void);

#endif /* GRPC_CORE_LIB_IOMGR_TCP_SERVER_UTILS_POSIX_H */
