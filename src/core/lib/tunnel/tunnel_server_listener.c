/*
 * tunnel_server_listener.c
 *
 *  Created on: Oct 31, 2016
 *      Author: gnirodi
 */

#include "src/core/lib/tunnel/tunnel_server_listener.h"

#include <grpc/grpc.h>
#include <grpc/support/alloc.h>
#include <grpc/support/log.h>
#include <grpc/support/string_util.h>
#include <grpc/support/useful.h>
#include "src/core/ext/transport/chttp2/transport/chttp2_transport.h"
#include "src/core/lib/channel/http_server_filter.h"
#include "src/core/lib/iomgr/resolve_address.h"
#include "src/core/lib/iomgr/tcp_server.h"
#include "src/core/lib/surface/api_trace.h"
#include "src/core/lib/tunnel/tunnel.h"

void new_tunnel_server_transport(grpc_exec_ctx *exec_ctx,
                                 tunnel_server_listener *listener,
                                 grpc_endpoint *tunneling_ep) {
  /*
   * Beware that the call to grpc_create_chttp2_transport() has to happen before
   * grpc_tcp_server_destroy(). This is fine here, but similar code
   * asynchronously doing a handshake instead of calling grpc_tcp_server_start()
   * (as in server_secure_chttp2.c) needs to add synchronization to avoid this
   * case.
   */
  grpc_transport *transport = grpc_create_chttp2_transport(
      exec_ctx, grpc_server_get_channel_args(listener->server),
      tunneling_ep, 0);
  size_t pollset_idx = (size_t) gpr_atm_no_barrier_fetch_add(
      &listener->next_pollset_to_assign, 1);
  grpc_pollset *accepting_pollset = listener->pollsets[pollset_idx %
                                                       listener->pollset_count];
  grpc_server_setup_transport(exec_ctx, listener->server, transport,
                              accepting_pollset,
                              grpc_server_get_channel_args(listener->server));
  grpc_chttp2_transport_start_reading(exec_ctx, transport, NULL, 0);
}

/* Server callback: start listening on our ports */
static void start(grpc_exec_ctx *exec_ctx, grpc_server *server,
                  void *tunnel_listener,
                  grpc_pollset **pollsets, size_t pollset_count) {
  tunnel_server_listener *listener = (tunnel_server_listener*)tunnel_listener;
  listener->pollsets = pollsets;
  listener->pollset_count = pollset_count;

  // Tell the tunnel to start listening on tunnel events for this listener
  listener->tunnel->vtable->on_tunnel_server_listener_start(listener);
}

/* Server callback: destroy the tcp listener (so we don't generate further
   callbacks) */
static void destroy(grpc_exec_ctx *exec_ctx, grpc_server *server,
                    void *tunnel_listener, grpc_closure *destroy_done) {
  tunnel_server_listener *listener = (tunnel_server_listener*)tunnel_listener;
  gpr_free(listener);
  grpc_exec_ctx_sched(exec_ctx, destroy_done, GRPC_ERROR_NONE, NULL);
}

int grpc_server_add_tunnel_listener(grpc_server *server, const char *addr,
                                    grpc_tunnel *tunnel) {
  tunnel_server_listener *tunnel_listener = NULL;
  grpc_exec_ctx exec_ctx = GRPC_EXEC_CTX_INIT;

  GRPC_API_TRACE(
      "grpc_server_add_tunnel_listener(server=%p, addr=%s, tunnel=%p)", 3,
                 (server, addr, tunnel));

  tunnel_listener =
      (tunnel_server_listener*) gpr_malloc(sizeof(*tunnel_listener));
  tunnel_listener->tunnel = tunnel;
  tunnel_listener->server = server;
  tunnel_listener->addr = addr;
  tunnel_listener->pollsets = NULL;
  tunnel_listener->pollset_count = (size_t) 0;
  gpr_atm_no_barrier_store(&tunnel_listener->next_pollset_to_assign, 0);

  /* Register with the server only upon success */
  grpc_server_add_listener(&exec_ctx, server, tunnel_listener, start, destroy);

  grpc_exec_ctx_finish(&exec_ctx);
  return 0;
}
