/*
 * tunnel_server_listener.h
 *
 *  Created on: Oct 31, 2016
 *      Author: gnirodi
 */

#ifndef SRC_CORE_LIB_TUNNEL_TUNNEL_SERVER_LISTENER_H_
#define SRC_CORE_LIB_TUNNEL_TUNNEL_SERVER_LISTENER_H_

#include <grpc/impl/codegen/atm.h>
#include <grpc/impl/codegen/sync_generic.h>
#include "src/core/lib/iomgr/endpoint.h"
#include "src/core/lib/surface/server.h"


// forward declaration
typedef grpc_tunnel grpc_tunnel;

/** Represents a server- tunnel combination that can listen in on
    new tunnel events, each of which result call newEndpoint on the listener */
typedef struct tunnel_server_listener {
  grpc_server *server;
  const char *addr;
  grpc_tunnel *tunnel;

  /** Pollsets are not entirely used by the tunnel. This is used to
      maintain compatibility with the grpc_server interface */
  grpc_pollset **pollsets;
  size_t pollset_count;
  gpr_atm next_pollset_to_assign;
} tunnel_server_listener;

/** Creates a tunnel_server_listener for the server, addr and tunnel */
int add_tunnel_server_listener(grpc_server *server, const char *addr,
                               grpc_tunnel *tunnel);

/** Called by a tunnel when a new endpoint becomes available for creating
    a transport */
void new_tunnel_server_transport(
    grpc_exec_ctx *exec_ctx, tunnel_server_listener *listener,
    grpc_endpoint *tunneling_ep);

#endif /* SRC_CORE_LIB_TUNNEL_TUNNEL_SERVER_LISTENER_H_ */
